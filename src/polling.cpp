#include "polling.hpp"
#include "registration_result.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>

namespace itu::polling {
namespace {
struct Stop { const char* reason; };
struct RefreshRequired {};
std::int64_t epoch_ms(WallClock::time_point value) {
    return std::chrono::duration_cast<Milliseconds>(value.time_since_epoch()).count();
}
template<class Clock>
typename Clock::time_point later(typename Clock::time_point value, Milliseconds delay) {
    using D = typename Clock::duration;
    const long double ticks = std::chrono::duration<long double, typename D::period>(delay).count();
    const long double result = static_cast<long double>(value.time_since_epoch().count()) + ticks;
    if (result > static_cast<long double>(D::max().count()) || result < static_cast<long double>(D::min().count()))
        throw Stop{"unrepresentable_deadline"};
    return value + std::chrono::duration_cast<D>(delay);
}
WallClock::time_point from_ms(std::int64_t value) {
    return later<WallClock>(WallClock::time_point{}, Milliseconds(value));
}

class Schedule {
    Runtime& runtime_;
    const Config& config_;
    WallClock::time_point wall_;
    SteadyClock::time_point steady_;
public:
    Schedule(Runtime& runtime, const Config& config)
        : runtime_(runtime), config_(config), wall_(runtime.wall_now()), steady_(runtime.steady_now()) {}
    void check(bool end = true) {
        if (runtime_.cancelled && runtime_.cancelled()) throw Stop{"interrupted"};
        const auto wall = runtime_.wall_now();
        const auto steady = runtime_.steady_now();
        const long double wd = std::chrono::duration<long double>(wall - wall_).count();
        const long double sd = std::chrono::duration<long double>(steady - steady_).count();
        // NTP slewing is tolerated; a step/resume discrepancy never compresses a wait.
        if (sd < 0 || std::abs(wd - sd) > 1.0L) throw Stop{"clock_discontinuity"};
        wall_ = wall;
        steady_ = steady;
        if (end && wall >= config_.end) throw Stop{"window_end"};
    }
    void until(WallClock::time_point target) {
        check();
        if (target >= config_.end) throw Stop{"window_end"};
        for (;;) {
            const auto now = runtime_.wall_now();
            if (target <= now) break;
            delay(std::chrono::ceil<Milliseconds>(target - now));
        }
    }
    void delay(Milliseconds delay) {
        check();
        const auto deadline = later<SteadyClock>(runtime_.steady_now(), delay);
        if (later<WallClock>(runtime_.wall_now(), delay) >= config_.end) throw Stop{"window_end"};
        while (runtime_.steady_now() < deadline) {
            check();
            auto remaining = std::chrono::ceil<Milliseconds>(deadline - runtime_.steady_now());
            runtime_.sleep(std::min(remaining, Milliseconds(100)));
        }
        check();
    }
};

// Reservations are durable before dispatch. Unresolved reservations do not
// expire: persistence may block, and a redirect chain can send its last request
// much later than admission. Completed chains are charged at completion; crash
// recovery charges unresolved reservations from recovery time conservatively.
class Governor {
    struct Event { std::int64_t at; std::uint64_t count; bool unresolved = false; };
    Runtime& runtime_;
    const Config& config_;
    Schedule& schedule_;
    std::deque<Event> events_;
    Milliseconds retention_;
    std::int64_t last_wall_ = 0, cooldown_ = 0;
    bool active_ = false;
    void prune() {
        const auto now = epoch_ms(runtime_.wall_now());
        while (!events_.empty() && !events_.front().unresolved && events_.front().at <= now - retention_.count()) events_.pop_front();
    }
    void save() {
        last_wall_ = std::max(last_wall_, epoch_ms(runtime_.wall_now()));
        Json values = Json::array();
        for (const auto& event : events_) values.push_back({{"at_ms", event.at}, {"count", event.count}, {"unresolved", event.unresolved}});
        runtime_.write_state(Json{{"version", 2}, {"window_seconds", std::chrono::duration_cast<std::chrono::seconds>(retention_).count()},
            {"last_wall_ms", last_wall_}, {"cooldown_until_ms", cooldown_}, {"events", values}}.dump());
    }
    void complete_reservation() {
        events_.back().at = std::max(last_wall_, epoch_ms(runtime_.wall_now()));
        events_.back().unresolved = false;
        active_ = false;
        save();
    }
public:
    Governor(Runtime& runtime, const Config& config, Schedule& schedule)
        : runtime_(runtime), config_(config), schedule_(schedule),
          retention_(std::chrono::duration_cast<Milliseconds>(config.request_budget.window)) {
        if (const auto stored = runtime_.read_state()) {
            try {
                const auto state = Json::parse(*stored);
                if (!state.is_object() || (state.at("version") != 1 && state.at("version") != 2) ||
                    !state.at("window_seconds").is_number_integer() ||
                    state.at("window_seconds").get<std::int64_t>() <= 0 ||
                    !state.at("last_wall_ms").is_number_integer() ||
                    !state.at("cooldown_until_ms").is_number_integer() || !state.at("events").is_array())
                    throw Stop{"invalid_budget_state"};
                last_wall_ = state.at("last_wall_ms").get<std::int64_t>();
                cooldown_ = state.at("cooldown_until_ms").get<std::int64_t>();
                from_ms(last_wall_); from_ms(cooldown_);
                const auto prior_window = state.at("window_seconds").get<std::int64_t>();
                if (prior_window > std::chrono::duration_cast<std::chrono::seconds>(WallClock::duration::max()).count() / 4)
                    throw Stop{"invalid_budget_state"};
                if (config.request_budget.window.count() > prior_window) {
                    // A shorter prior window may already have discarded history.
                    // Wait out the new horizon before admitting any new transfer.
                    cooldown_ = std::max(cooldown_, epoch_ms(later<WallClock>(from_ms(last_wall_), retention_)));
                }
                retention_ = std::max(retention_, Milliseconds(prior_window * 1000));
                if (last_wall_ > epoch_ms(runtime.wall_now())) throw Stop{"clock_discontinuity"};
                const bool legacy = state.at("version") == 1;
                if (legacy) {
                    // Version 1 stored admission time without an in-flight bit.
                    // Its last request may have occurred after that timestamp.
                    cooldown_ = std::max(cooldown_, epoch_ms(later<WallClock>(runtime.wall_now(), retention_)));
                }
                for (const auto& item : state.at("events")) {
                    if (!item.at("at_ms").is_number_integer() || !item.at("count").is_number_unsigned() ||
                        (!legacy && !item.at("unresolved").is_boolean()))
                        throw Stop{"invalid_budget_state"};
                    Event event{item.at("at_ms").get<std::int64_t>(), item.at("count").get<std::uint64_t>(),
                        !legacy && item.at("unresolved").get<bool>()};
                    if (event.count == 0 || event.at > last_wall_ || (!events_.empty() &&
                        (events_.back().unresolved || event.at < events_.back().at)))
                        throw Stop{"invalid_budget_state"};
                    from_ms(event.at);
                    events_.push_back(event);
                }
                if (!events_.empty() && events_.back().unresolved) {
                    events_.back().at = epoch_ms(runtime.wall_now());
                    events_.back().unresolved = false;
                }
            } catch (const Json::exception&) { throw Stop{"invalid_budget_state"}; }
        }
        prune();
        save();
    }
    std::uint64_t used() const {
        std::uint64_t sum = 0;
        const auto cutoff = epoch_ms(runtime_.wall_now()) - std::chrono::duration_cast<Milliseconds>(config_.request_budget.window).count();
        for (const auto& event : events_) {
            if (!event.unresolved && event.at <= cutoff) continue;
            if (event.count > std::numeric_limits<std::uint64_t>::max() - sum) throw Stop{"invalid_budget_state"};
            sum += event.count;
        }
        return sum;
    }
    void before(const HttpRequest& request, const std::function<void()>& guard = {}) {
        schedule_.check();
        if (active_ || request.max_redirects < 0 || request.max_redirects > 100) throw Stop{"invalid_reservation"};
        const std::uint64_t reservation = request.follow_redirects ? static_cast<std::uint64_t>(request.max_redirects) + 1 : 1;
        if (reservation > config_.request_budget.count) throw Stop{"budget_cannot_admit_transfer"};
        if (cooldown_ > epoch_ms(runtime_.wall_now())) schedule_.until(from_ms(cooldown_));
        for (;;) {
            prune();
            if (used() <= config_.request_budget.count - reservation) break;
            const auto cutoff = epoch_ms(runtime_.wall_now()) - std::chrono::duration_cast<Milliseconds>(config_.request_budget.window).count();
            const auto first_used = std::find_if(events_.begin(), events_.end(), [&](const Event& event) { return event.at > cutoff; });
            if (first_used == events_.end()) throw Stop{"invalid_budget_state"};
            schedule_.until(later<WallClock>(from_ms(first_used->at),
                std::chrono::duration_cast<Milliseconds>(config_.request_budget.window)));
        }
        schedule_.check();
        if (guard) guard();
        events_.push_back({epoch_ms(runtime_.wall_now()), reservation, true});
        save();
        active_ = true;
        // Filesystem persistence can block. Recheck at the actual dispatch
        // boundary; an abandoned reservation remains conservatively counted.
        try {
            schedule_.check();
            if (guard) guard();
        } catch (...) { abandon(); throw; }
    }
    void after(const HttpTransferInfo& info) {
        if (!active_ || events_.empty()) throw Stop{"invalid_reservation"};
        if (const auto retry = retry_after(info.retry_after, runtime_.wall_now()))
            cooldown_ = std::max(cooldown_, epoch_ms(*retry));
        if (info.request_count && *info.request_count > 0 && *info.request_count <= events_.back().count)
            events_.back().count = *info.request_count;
        complete_reservation();
    }
    void abandon() { if (active_) complete_reservation(); }
    void cooldown(WallClock::time_point until) {
        cooldown_ = std::max(cooldown_, epoch_ms(until));
        save();
    }
};
} // namespace

IntervalSampler::IntervalSampler(const Config& config) : config_(config) {
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
    random_.seed(seed);
}
IntervalSampler::IntervalSampler(const Config& config, std::uint64_t seed) : config_(config), random_(seed) {}
Milliseconds IntervalSampler::next() {
    const auto lo = config_.min_interval.count(), hi = config_.max_interval.count();
    double fraction = 0;
    if (config_.distribution == Distribution::uniform) {
        fraction = std::generate_canonical<double, 53>(random_);
    } else {
        const double p = static_cast<double>(config_.expected_interval.count() - lo) / static_cast<double>(hi - lo);
        std::gamma_distribution<double> alpha(config_.beta_concentration * p, 1);
        std::gamma_distribution<double> beta(config_.beta_concentration * (1 - p), 1);
        bool sampled = false;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto x = alpha(random_), y = beta(random_);
            if (std::isfinite(x) && std::isfinite(y) && (x > 0 || y > 0)) {
                fraction = x > y ? 1 / (1 + y / x) : (x / y) / (1 + x / y);
                sampled = true;
                break;
            }
        }
        if (!sampled) throw Stop{"invalid_distribution_sample"};
    }
    const long double value = static_cast<long double>(lo) + static_cast<long double>(hi - lo) * fraction;
    return Milliseconds(std::clamp(static_cast<std::int64_t>(std::ceil(value)), lo, hi));
}
Milliseconds backoff(const Config& config, std::uint64_t streak) {
    auto value = config.backoff_base.count();
    const auto limit = config.backoff_max.count();
    for (std::uint64_t i = 1; i < streak && value < limit; ++i) value = value > limit / 2 ? limit : value * 2;
    return Milliseconds(std::min(value, limit));
}
std::optional<WallClock::time_point> retry_after(const std::string& value, WallClock::time_point now) {
    if (value.empty() || value.size() > 128) return std::nullopt;
    try {
        if (std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
            std::uint64_t seconds = 0;
            for (char c : value) {
                if (seconds > (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 1000) - (c - '0')) / 10)
                    return std::nullopt;
                seconds = seconds * 10 + static_cast<unsigned>(c - '0');
            }
            return later<WallClock>(now, Milliseconds(static_cast<std::int64_t>(seconds * 1000)));
        }
        const auto parsed = curl_getdate(value.c_str(), nullptr);
        if (parsed < 0 || parsed > std::numeric_limits<std::int64_t>::max() / 1000) return std::nullopt;
        return std::max(now, from_ms(static_cast<std::int64_t>(parsed) * 1000));
    } catch (const Stop&) { return std::nullopt; }
}
Json clock_report(const platform::ClockHealth& health) {
    Json report{{"provider", health.provider}, {"reason", health.reason}};
    report["network_time_enabled"] = health.network_time_enabled ? Json(*health.network_time_enabled) : Json(nullptr);
    report["synchronized"] = health.synchronized ? Json(*health.synchronized) : Json(nullptr);
    report["max_error_us"] = health.max_error_us ? Json(*health.max_error_us) : Json(nullptr);
    report["estimated_error_us"] = health.estimated_error_us ? Json(*health.estimated_error_us) : Json(nullptr);
    report["age_ms"] = health.age_ms ? Json(*health.age_ms) : Json(nullptr);
    return report;
}

RunResult run(const Config& config, HttpRequest registration, Runtime& runtime, const Options& options) {
    const auto started = runtime.steady_now();
    std::uint64_t attempts = 0, streak = 0, refreshes = 0, relogins = 0;
    std::vector<std::string> pending = config.crns;
    bool registration_outcome_unresolved = false;
    auto event = [&](const char* type, Json fields = Json::object()) {
        fields["event"] = type;
        fields["timestamp_ms"] = epoch_ms(runtime.wall_now());
        fields["elapsed_ms"] = std::chrono::duration_cast<Milliseconds>(runtime.steady_now() - started).count();
        fields["attempt"] = attempts;
        fields["pending_count"] = pending.size();
        fields["completed_count"] = config.crns.size() - pending.size();
        if (runtime.log) runtime.log(fields);
    };
    auto finish = [&](bool success, const char* reason) {
        // A failed diagnostic sink must not erase the outcome or its manual
        // OBS verification guidance after a registration may have been sent.
        try { event("stop", {{"reason", reason}}); } catch (...) {}
        return RunResult{success, reason, attempts};
    };
    struct ResetObservers {
        Runtime& runtime;
        ~ResetObservers() { if (runtime.observers) runtime.observers({}, {}); }
    };
    try {
        if (!config.enabled || (options.test && !options.dry_run)) throw Stop{"invalid_polling_mode"};
        Schedule schedule(runtime, config);
        Json previous_clock;
        const auto check_clock = [&] {
            if (runtime.clock_health) {
                const auto health = runtime.clock_health();
                const auto report = clock_report(health);
                if (report != previous_clock) { event("clock", report); previous_clock = report; }
                if (health.synchronized && !*health.synchronized) throw Stop{"clock_unsynchronized"};
            }
            schedule.check();
        };
        schedule.check(); check_clock();
        Governor governor(runtime, config, schedule);
        ResetObservers reset{runtime};
        TokenResult token;
        bool registration_transfer = false;
        const auto token_ready = [&] {
            if (token.expires_at && *token.expires_at <= later<WallClock>(runtime.wall_now(), Milliseconds(35000)))
                throw RefreshRequired{};
        };
        const auto dispatch_guard = [&] {
            check_clock();
            if (registration_transfer) token_ready();
        };
        runtime.observers([&](const HttpRequest& request) {
            governor.before(request, dispatch_guard);
            event("admission", {{"budget_used", governor.used()}, {"max_redirects", request.follow_redirects ? request.max_redirects : 0}});
            // Logging is caller-provided and may itself block. Admission is
            // not permission to dispatch after the window or JWT expires.
            try {
                dispatch_guard();
                if (registration_transfer) {
                    ++attempts;
                    try { event("attempt"); dispatch_guard(); }
                    catch (...) { --attempts; throw; }
                }
            } catch (...) { governor.abandon(); throw; }
            // This is the final step before the transport can send the POST.
            // Keep uncertainty until transport proves no dispatch or a complete
            // application response has been classified, including local errors
            // in after-transfer persistence/logging or response inspection.
            if (registration_transfer) registration_outcome_unresolved = true;
        }, [&](const HttpTransferInfo& info) {
            if (registration_transfer && info.proven_pre_dispatch)
                registration_outcome_unresolved = false;
            governor.after(info);
            const auto retry = retry_after(info.retry_after, runtime.wall_now());
            event("transfer", {{"http_status", info.http_status}, {"curl_code", info.curl_code},
                {"pre_dispatch", info.proven_pre_dispatch}, {"budget_used", governor.used()},
                {"retry_after_ms", retry ? Json(epoch_ms(*retry)) : Json(nullptr)}});
        });
        const auto authenticate = [&](const std::function<TokenResult()>& operation) {
            try { return operation(); }
            catch (const AuthError& error) {
                const auto retry = retry_after(error.retry_after, runtime.wall_now());
                if (retry) governor.cooldown(*retry);
                event("authentication_response", {{"http_status", error.status},
                    {"retry_after_ms", retry ? Json(epoch_ms(*retry)) : Json(nullptr)}});
                throw;
            }
            catch (const HttpTransportError& error) {
                const auto retry = retry_after(error.retry_after, runtime.wall_now());
                if (retry) governor.cooldown(*retry);
                throw;
            }
        };
        event("start", {{"distribution", config.distribution == Distribution::beta ? "beta" : "uniform"},
            {"minimum_ms", config.min_interval.count()}, {"maximum_ms", config.max_interval.count()},
            {"expected_ms", config.expected_interval.count()}, {"approx_requests_per_hour", 3600000.0 / config.expected_interval.count()}});
        if (!(options.test && options.dry_run)) schedule.until(later<WallClock>(config.start, Milliseconds(-60000)));
        token = authenticate(runtime.login);
        event("jwt_acquired");
        auto ensure_token = [&] {
            while (token.expires_at && *token.expires_at <= later<WallClock>(runtime.wall_now(), Milliseconds(35000))) {
                if (refreshes >= 2) throw Stop{"jwt_refresh_limit"};
                ++refreshes;
                event("jwt_refresh", {{"refresh_count", refreshes}});
                try { token = authenticate(runtime.refresh); }
                catch (const AuthError& error) {
                    if ((error.kind != AuthError::Kind::unauthorized && error.kind != AuthError::Kind::login_required) || relogins >= 1)
                        throw;
                    ++relogins;
                    event("jwt_reauthentication", {{"reauthentication_count", relogins}});
                    token = authenticate(runtime.login);
                }
            }
            if (token.bearer.empty()) throw Stop{"authentication_failed"};
        };
        auto prepare = [&] {
            registration.method = "POST";
            registration.follow_redirects = false;
            registration.max_redirects = 0;
            registration.timeout_ms = 30000;
            registration.cancelled = runtime.cancelled;
            registration.body = Json{{"ECRN", pending}, {"SCRN", Json::array()}}.dump();
            registration.headers.erase(std::remove_if(registration.headers.begin(), registration.headers.end(),
                [](const std::string& value) { return value.rfind("Authorization:", 0) == 0; }), registration.headers.end());
            registration.headers.push_back("Authorization: " + token.bearer);
        };
        ensure_token(); prepare();
        if (options.dry_run) {
            schedule.check();
            if (!runtime.prepare) throw Stop{"request_preparation_unavailable"};
            runtime.prepare(registration);
            schedule.check();
            event("prepared");
            return finish(true, "dry_run");
        }
        schedule.until(config.start);
        IntervalSampler sampler(config);
        while (!pending.empty()) {
            schedule.check(); check_clock();
            if (attempts >= config.max_attempts) return finish(false, "attempt_limit");
            ensure_token(); prepare();
            schedule.check();
            if (runtime.wall_now() < config.start) throw Stop{"clock_discontinuity"};
            HttpResponse response;
            registration_transfer = true;
            try {
                response = runtime.request(registration);
                registration_transfer = false;
            } catch (const RefreshRequired&) {
                registration_transfer = false;
                // No transfer started; renew once back outside the transport
                // callback, then obtain a fresh durable admission.
                continue;
            }
            catch (const HttpTransportError& error) {
                registration_transfer = false;
                if (error.proven_pre_dispatch) registration_outcome_unresolved = false;
                const auto retry = retry_after(error.retry_after, runtime.wall_now());
                event("response", {{"http_status", error.http_status}, {"curl_code", error.curl_code},
                    {"category", "transport_failure"}, {"replay_safe", error.proven_pre_dispatch},
                    {"retry_after_ms", retry ? Json(epoch_ms(*retry)) : Json(nullptr)}});
                if (error.http_status == 429) return finish(false, "rate_limited");
                if (error.cancelled) return finish(false, "interrupted_unknown_outcome");
                if (!error.proven_pre_dispatch) return finish(false, "unknown_outcome");
                const auto delay = backoff(config, ++streak);
                event("backoff", {{"backoff_streak", streak}, {"next_interval_ms", delay.count()}});
                schedule.delay(delay);
                continue;
            }
            const auto classified = classify(response, pending);
            if (classified.action != Action::stop_unknown)
                registration_outcome_unresolved = false;
            const auto header = response.headers.find("retry-after");
            const auto retry = header == response.headers.end() ? std::optional<WallClock::time_point>{} : retry_after(header->second, runtime.wall_now());
            if (retry) governor.cooldown(*retry);
            event("response", {{"http_status", response.status}, {"category", category_name(classified.category)},
                {"action", action_name(classified.action)}, {"codes", classified.codes},
                {"retry_after_ms", retry ? Json(epoch_ms(*retry)) : Json(nullptr)}});
            for (const auto& crn : classified.satisfied)
                pending.erase(std::remove(pending.begin(), pending.end(), crn), pending.end());
            if (classified.action == Action::stop_success) return finish(true, "all_satisfied");
            if (classified.action == Action::stop_rate_limit) return finish(false, "rate_limited");
            if (classified.action == Action::stop_action_required) return finish(false, "action_required");
            if (classified.action != Action::retry_normal) return finish(false, "unknown_outcome");
            streak = 0;
            const auto delay = runtime.interval ? runtime.interval() : sampler.next();
            if (delay < config.min_interval || delay > config.max_interval) throw Stop{"invalid_distribution_sample"};
            event("interval", {{"next_interval_ms", delay.count()}, {"backoff_streak", streak},
                {"deadline_ms", epoch_ms(later<WallClock>(runtime.wall_now(), delay))}});
            schedule.delay(delay);
        }
        return finish(true, "all_satisfied");
    } catch (const Stop& stop) {
        return finish(false, registration_outcome_unresolved ? "unknown_outcome" : stop.reason);
    }
    catch (const AuthError& error) {
        event("authentication_failure", {{"http_status", error.status}});
        // No login loop, generic 403 expiry assumption, or renewal to evade blocks.
        return finish(false, error.status == 429 ? "rate_limited" : "authentication_failed");
    } catch (const HttpTransportError& error) {
        const auto retry = retry_after(error.retry_after, runtime.wall_now());
        event("authentication_transport_failure", {{"http_status", error.http_status},
            {"curl_code", error.curl_code},
            {"retry_after_ms", retry ? Json(epoch_ms(*retry)) : Json(nullptr)}});
        if (error.http_status == 429) return finish(false, "rate_limited");
        return finish(false, error.cancelled ? "interrupted" : "authentication_failed");
    } catch (const std::exception&) {
        return finish(false, registration_outcome_unresolved ? "unknown_outcome" : "local_failure");
    }
}
} // namespace itu::polling
