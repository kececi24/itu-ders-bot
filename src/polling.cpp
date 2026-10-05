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
        if (end && wall >= config_.end) throw Stop{"window_end"};
    }
    void health() {
        if (runtime_.clock_health) {
            const auto health = runtime_.clock_health();
            if (health.synchronized && !*health.synchronized) throw Stop{"clock_unsynchronized"};
        }
    }
    void until(WallClock::time_point target) {
        check();
        if (target >= config_.end) throw Stop{"window_end"};
        const auto now = runtime_.wall_now();
        if (target <= now) return;
        delay(std::chrono::ceil<Milliseconds>(target - now));
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

// Reservations are durable before dispatch. A crash/uncertain transport keeps
// the full redirect allowance; only observed successful transfers refund it.
class Governor {
    struct Event { std::int64_t at; std::uint64_t count; };
    Runtime& runtime_;
    const Config& config_;
    Schedule& schedule_;
    std::deque<Event> events_;
    std::int64_t last_wall_ = 0, cooldown_ = 0;
    bool active_ = false;
    void prune() {
        const auto now = epoch_ms(runtime_.wall_now());
        const auto window = std::chrono::duration_cast<Milliseconds>(config_.request_budget.window).count();
        while (!events_.empty() && events_.front().at <= now - window) events_.pop_front();
    }
    void save() {
        last_wall_ = epoch_ms(runtime_.wall_now());
        Json values = Json::array();
        for (const auto& event : events_) values.push_back({{"at_ms", event.at}, {"count", event.count}});
        runtime_.write_state(Json{{"version", 1}, {"window_seconds", config_.request_budget.window.count()},
            {"last_wall_ms", last_wall_}, {"cooldown_until_ms", cooldown_}, {"events", values}}.dump());
    }
public:
    Governor(Runtime& runtime, const Config& config, Schedule& schedule)
        : runtime_(runtime), config_(config), schedule_(schedule) {
        if (const auto stored = runtime_.read_state()) {
            try {
                const auto state = Json::parse(*stored);
                if (!state.is_object() || state.at("version") != 1 ||
                    !state.at("window_seconds").is_number_integer() ||
                    state.at("window_seconds").get<std::int64_t>() != config.request_budget.window.count() ||
                    !state.at("last_wall_ms").is_number_integer() ||
                    !state.at("cooldown_until_ms").is_number_integer() || !state.at("events").is_array())
                    throw Stop{"invalid_budget_state"};
                last_wall_ = state.at("last_wall_ms").get<std::int64_t>();
                cooldown_ = state.at("cooldown_until_ms").get<std::int64_t>();
                from_ms(last_wall_); from_ms(cooldown_);
                if (last_wall_ > epoch_ms(runtime.wall_now())) throw Stop{"clock_discontinuity"};
                for (const auto& item : state.at("events")) {
                    if (!item.at("at_ms").is_number_integer() || !item.at("count").is_number_unsigned())
                        throw Stop{"invalid_budget_state"};
                    const Event event{item.at("at_ms").get<std::int64_t>(), item.at("count").get<std::uint64_t>()};
                    if (event.count == 0 || event.at > last_wall_ || (!events_.empty() && event.at < events_.back().at))
                        throw Stop{"invalid_budget_state"};
                    from_ms(event.at);
                    events_.push_back(event);
                }
            } catch (const Json::exception&) { throw Stop{"invalid_budget_state"}; }
        }
        prune();
        save();
    }
    std::uint64_t used() const {
        std::uint64_t sum = 0;
        for (const auto& event : events_) {
            if (event.count > std::numeric_limits<std::uint64_t>::max() - sum) throw Stop{"invalid_budget_state"};
            sum += event.count;
        }
        return sum;
    }
    void before(const HttpRequest& request) {
        schedule_.check();
        if (active_ || request.max_redirects < 0 || request.max_redirects > 100) throw Stop{"invalid_reservation"};
        const std::uint64_t reservation = request.follow_redirects ? static_cast<std::uint64_t>(request.max_redirects) + 1 : 1;
        if (reservation > config_.request_budget.count) throw Stop{"budget_cannot_admit_transfer"};
        if (cooldown_ > epoch_ms(runtime_.wall_now())) schedule_.until(from_ms(cooldown_));
        for (;;) {
            prune();
            if (used() <= config_.request_budget.count - reservation) break;
            if (events_.empty()) throw Stop{"invalid_budget_state"};
            schedule_.until(later<WallClock>(from_ms(events_.front().at),
                std::chrono::duration_cast<Milliseconds>(config_.request_budget.window)));
        }
        schedule_.check();
        active_ = true;
        events_.push_back({epoch_ms(runtime_.wall_now()), reservation});
        save();
    }
    void after(const HttpTransferInfo& info) {
        if (!active_ || events_.empty()) throw Stop{"invalid_reservation"};
        if (info.request_count && *info.request_count > 0 && *info.request_count <= events_.back().count)
            events_.back().count = *info.request_count;
        active_ = false;
        save();
    }
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
        if (parsed < 0 || static_cast<long double>(parsed) * 1000 > std::numeric_limits<std::int64_t>::max()) return std::nullopt;
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
        event("stop", {{"reason", reason}});
        return RunResult{success, reason, attempts};
    };
    struct ResetObservers {
        Runtime& runtime;
        ~ResetObservers() { if (runtime.observers) runtime.observers({}, {}); }
    } reset{runtime};
    try {
        if (!config.enabled || (options.test && !options.dry_run)) throw Stop{"invalid_polling_mode"};
        Schedule schedule(runtime, config);
        schedule.check(); schedule.health();
        Governor governor(runtime, config, schedule);
        runtime.observers([&](const HttpRequest& request) {
            governor.before(request);
            event("admission", {{"budget_used", governor.used()}, {"max_redirects", request.follow_redirects ? request.max_redirects : 0}});
        }, [&](const HttpTransferInfo& info) {
            governor.after(info);
            event("transfer", {{"http_status", info.http_status}, {"curl_code", info.curl_code},
                {"pre_dispatch", info.proven_pre_dispatch}, {"budget_used", governor.used()}});
        });
        event("start", {{"distribution", config.distribution == Distribution::beta ? "beta" : "uniform"},
            {"minimum_ms", config.min_interval.count()}, {"maximum_ms", config.max_interval.count()},
            {"expected_ms", config.expected_interval.count()}, {"approx_requests_per_hour", 3600000.0 / config.expected_interval.count()}});
        if (!(options.test && options.dry_run)) schedule.until(later<WallClock>(config.start, Milliseconds(-60000)));
        TokenResult token = runtime.login();
        event("jwt_acquired");
        auto ensure_token = [&] {
            while (token.expires_at && *token.expires_at < later<WallClock>(runtime.wall_now(), Milliseconds(35000))) {
                if (refreshes >= 2) throw Stop{"jwt_refresh_limit"};
                ++refreshes;
                event("jwt_refresh", {{"refresh_count", refreshes}});
                try { token = runtime.refresh(); }
                catch (const AuthError& error) {
                    if ((error.kind != AuthError::Kind::unauthorized && error.kind != AuthError::Kind::login_required) || relogins >= 1)
                        throw;
                    ++relogins;
                    event("jwt_reauthentication", {{"reauthentication_count", relogins}});
                    token = runtime.login();
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
        if (options.dry_run) { event("prepared"); return finish(true, "dry_run"); }
        schedule.until(config.start);
        IntervalSampler sampler(config);
        while (!pending.empty()) {
            schedule.check(); schedule.health();
            if (attempts >= config.max_attempts) return finish(false, "attempt_limit");
            ensure_token(); prepare();
            schedule.check();
            if (runtime.wall_now() < config.start) throw Stop{"clock_discontinuity"};
            ++attempts;
            event("attempt");
            HttpResponse response;
            try { response = runtime.request(registration); }
            catch (const HttpTransportError& error) {
                event("response", {{"http_status", error.http_status}, {"curl_code", error.curl_code},
                    {"category", "transport_failure"}, {"replay_safe", error.proven_pre_dispatch}});
                if (error.cancelled) return finish(false, "interrupted_unknown_outcome");
                if (!error.proven_pre_dispatch) return finish(false, "unknown_outcome");
                const auto delay = backoff(config, ++streak);
                event("backoff", {{"backoff_streak", streak}, {"next_interval_ms", delay.count()}});
                schedule.delay(delay);
                continue;
            }
            const auto classified = classify(response, pending);
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
    } catch (const Stop& stop) { return finish(false, stop.reason); }
    catch (const AuthError& error) {
        event("authentication_failure", {{"http_status", error.status}});
        // No login loop, generic 403 expiry assumption, or renewal to evade blocks.
        return finish(false, error.status == 429 ? "rate_limited" : "authentication_failed");
    } catch (const HttpTransportError& error) {
        event("authentication_transport_failure", {{"http_status", error.http_status}, {"curl_code", error.curl_code}});
        return finish(false, error.cancelled ? "interrupted" : "authentication_failed");
    } catch (const std::exception&) { return finish(false, "local_failure"); }
}
} // namespace itu::polling
