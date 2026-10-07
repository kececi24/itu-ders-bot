#include "src/polling.hpp"
#include "src/registration_result.hpp"
#include "include/polling_config.hpp"
#include "tests/test_helpers.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace itu::polling;
using nlohmann::json;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Config make_test_config(WallClock::time_point start_tp) {
    Config config;
    config.enabled = true;
    config.start = start_tp;
    config.end = start_tp + std::chrono::minutes(10);
    config.distribution = Distribution::beta;
    config.min_interval = std::chrono::milliseconds(3000);
    config.max_interval = std::chrono::milliseconds(10000);
    config.expected_interval = std::chrono::milliseconds(5000);
    config.beta_concentration = 6.0;
    config.request_budget.count = 20;
    config.request_budget.window = std::chrono::seconds(3600);
    config.max_attempts = 5;
    config.backoff_base = std::chrono::milliseconds(30000);
    config.backoff_max = std::chrono::milliseconds(300000);
    config.crns = {"001", "002"};
    return config;
}

struct TestHarness {
    WallClock::time_point wall;
    SteadyClock::time_point steady;
    std::string persisted_state;
    std::vector<json> logs;
    std::vector<HttpRequest> requests_made;
    std::vector<HttpRequest> prepared_requests;
    std::vector<std::string> registration_bodies;
    bool is_cancelled = false;
    itu::platform::ClockHealth health;
    std::function<HttpResponse(const HttpRequest&)> on_request;
    std::function<TokenResult()> on_login;
    std::function<TokenResult()> on_refresh;
    int logins = 0, refreshes = 0;
    std::chrono::milliseconds sleep_total{0};
    HttpSession::BeforeTransfer before_observer;
    HttpSession::AfterTransfer after_observer;

    TestHarness(WallClock::time_point start_wall = WallClock::now())
        : wall(start_wall), steady(SteadyClock::now()) {
        health.provider = "test";
        health.reason = "test";
        health.synchronized = true;
        health.network_time_enabled = true;
    }

    void advance(std::chrono::milliseconds duration) {
        wall += duration;
        steady += duration;
    }

    Runtime make_runtime() {
        Runtime runtime;
        runtime.wall_now = [this] { return wall; };
        runtime.steady_now = [this] { return steady; };
        runtime.sleep = [this](std::chrono::milliseconds ms) {
            sleep_total += ms;
            advance(ms);
        };
        runtime.cancelled = [this] { return is_cancelled; };
        runtime.clock_health = [this] { return health; };
        runtime.read_state = [this]() -> std::optional<std::string> {
            if (persisted_state.empty()) return std::nullopt;
            return persisted_state;
        };
        runtime.write_state = [this](const std::string& state) {
            persisted_state = state;
        };
        runtime.observers = [this](HttpSession::BeforeTransfer b, HttpSession::AfterTransfer a) {
            before_observer = std::move(b);
            after_observer = std::move(a);
        };
        runtime.login = [this] {
            ++logins;
            if (on_login) return on_login();
            return TokenResult{"Bearer test-jwt-token", wall + std::chrono::hours(1)};
        };
        runtime.refresh = [this] {
            ++refreshes;
            if (on_refresh) return on_refresh();
            return TokenResult{"Bearer test-refreshed-jwt", wall + std::chrono::hours(1)};
        };
        runtime.prepare = [this](const HttpRequest& req) { prepared_requests.push_back(req); };
        runtime.request = [this](const HttpRequest& req) -> HttpResponse {
            if (before_observer) before_observer(req);
            requests_made.push_back(req);
            if (req.method == "POST") registration_bodies.push_back(req.body);
            HttpResponse resp;
            try {
                if (on_request) resp = on_request(req);
                else {
                    resp.status = 200;
                    resp.body = "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"successResult\"}]}";
                }
            } catch (const HttpTransportError& err) {
                if (after_observer) {
                    HttpTransferInfo info;
                    info.curl_code = err.curl_code;
                    info.proven_pre_dispatch = err.proven_pre_dispatch;
                    info.cancelled = err.cancelled;
                    info.http_status = err.http_status;
                    info.retry_after = err.retry_after;
                    after_observer(info);
                }
                throw;
            }
            if (after_observer) {
                HttpTransferInfo info;
                info.curl_code = 0;
                info.http_status = resp.status;
                info.request_count = 1;
                const auto retry = resp.headers.find("retry-after");
                if (retry != resp.headers.end()) info.retry_after = retry->second;
                after_observer(info);
            }
            return resp;
        };
        runtime.log = [this](const json& event) {
            logs.push_back(event);
        };
        runtime.interval = [] { return std::chrono::milliseconds(3000); };
        return runtime;
    }
};

void test_dry_run() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    auto runtime = harness.make_runtime();
    HttpRequest req;
    Options options;
    options.dry_run = true;
    const auto result = run(config, req, runtime, options);
    check(result.success, "dry run should succeed");
    check(result.reason == "dry_run", "dry run reason mismatch");
    check(result.attempts == 0, "dry run must perform 0 attempts");
    check(harness.requests_made.empty(), "dry run must make zero registration requests");
    check(harness.logins == 1, "dry run must acquire initial token");
    check(harness.prepared_requests.size() == 1, "dry run must prepare actual request");
    check(harness.prepared_requests.front().method == "POST" &&
        !harness.prepared_requests.front().follow_redirects &&
        harness.prepared_requests.front().timeout_ms == 30000,
        "dry run must prepare production registration options");
    check(json::parse(harness.prepared_requests.front().body).at("SCRN").empty(), "dry run is add-only");
}

void test_immediate_all_success() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(result.success, "all success should succeed");
    check(result.reason == "all_satisfied", "all success reason mismatch");
    check(result.attempts == 1, "all success should take 1 attempt");
    check(harness.registration_bodies.size() == 1, "exactly 1 registration post");
    const auto payload = json::parse(harness.registration_bodies.front());
    check(payload["ECRN"] == json::array({"001", "002"}), "payload must have submitted CRNs");
}

void test_partial_success_then_completion() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    int post_count = 0;
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        ++post_count;
        if (post_count == 1) {
            return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"VAL06\"}]}", {}, {}};
        }
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"002\",\"resultCode\":\"VAL03\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(result.success, "partial then complete should succeed");
    check(result.reason == "all_satisfied", "reason should be all_satisfied");
    check(result.attempts == 2, "attempts should be 2");
    check(harness.registration_bodies.size() == 2, "2 registration POSTs");
    const auto first_payload = json::parse(harness.registration_bodies[0]);
    const auto second_payload = json::parse(harness.registration_bodies[1]);
    check(first_payload["ECRN"] == json::array({"001", "002"}), "first payload has both");
    check(second_payload["ECRN"] == json::array({"002"}), "second payload has only residual 002");
}

void test_business_rejection_no_backoff() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.max_attempts = 3;
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"VAL06\"},{\"crn\":\"002\",\"resultCode\":\"VAL02\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "all business rejection should exhaust attempts");
    check(result.reason == "attempt_limit", "should stop on attempt limit");
    check(result.attempts == 3, "should execute max_attempts");
    for (const auto& log : harness.logs) {
        if (log.contains("event") && log["event"] == "backoff") {
            throw std::runtime_error("business rejection must never trigger exponential backoff");
        }
    }
}

void test_pre_dispatch_failure_and_backoff() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    int call_count = 0;
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        ++call_count;
        if (call_count == 1) {
            throw HttpTransportError(7, 10, 0, 0, true, false);
        }
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"successResult\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(result.success, "should succeed after backoff");
    check(result.attempts == 2, "attempt count reflects two attempts");
    bool saw_backoff = false;
    for (const auto& log : harness.logs) {
        if (log.contains("event") && log["event"] == "backoff") {
            saw_backoff = true;
            check(log["backoff_streak"] == 1, "streak should be 1");
            check(log["next_interval_ms"] == 30000, "base backoff 30s");
        }
    }
    check(saw_backoff, "proven pre-dispatch failure must trigger backoff");
}

void test_in_flight_timeout_fails_closed() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        throw HttpTransportError(28, 30000, 0, 0, false, false);
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "in-flight failure must fail");
    check(result.reason == "unknown_outcome", "in-flight timeout must stop with unknown_outcome");
    check(result.attempts == 1, "must never retry ambiguous in-flight timeout");
}

void test_truncated_rate_limit_retains_cooldown() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    const auto start = harness.wall;
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        throw HttpTransportError(18, 1, 429, 0, false, false, 0, "120");
    };
    auto runtime = harness.make_runtime();
    const auto result = run(config, {}, runtime);
    check(!result.success && result.reason == "rate_limited" && result.attempts == 1,
        "truncated 429 must stop without replay");
    check(json::parse(harness.persisted_state).at("cooldown_until_ms") ==
        std::chrono::duration_cast<std::chrono::milliseconds>((start + std::chrono::seconds(120)).time_since_epoch()).count(),
        "truncated rate-limit Retry-After must persist");
}

void test_rate_limited_stop() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        HttpResponse resp;
        resp.status = 429;
        resp.headers["retry-after"] = "60";
        return resp;
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "rate limit must stop");
    check(result.reason == "rate_limited", "must report rate_limited");
    check(result.attempts == 1, "must not retry rate limit");
}

void test_window_end_stop() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.end = harness.wall + std::chrono::seconds(2);
    auto runtime = harness.make_runtime();
    runtime.interval = [] { return std::chrono::milliseconds(5000); };
    int count = 0;
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        ++count;
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"VAL06\"},{\"crn\":\"002\",\"resultCode\":\"VAL06\"}]}", {}, {}};
    };
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "window end must fail");
    check(result.reason == "window_end", "reason must be window_end");
}

void test_clock_unsynchronized_stop() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.health.synchronized = false;
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "unsynchronized clock must fail");
    check(result.reason == "clock_unsynchronized", "reason must be clock_unsynchronized");
    check(harness.requests_made.empty(), "must not make any requests when unsynchronized");
}

void test_clock_discontinuity_stop() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    auto runtime = harness.make_runtime();
    runtime.wall_now = [&, call = 0]() mutable {
        ++call;
        if (call > 2) return harness.wall - std::chrono::seconds(10);
        return harness.wall;
    };
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "discontinuity must fail");
    check(result.reason == "clock_discontinuity", "reason must be clock_discontinuity");
}

void test_cancellation() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.is_cancelled = true;
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "cancelled must fail");
    check(result.reason == "interrupted", "reason must be interrupted");
    check(harness.requests_made.empty(), "no requests if cancelled");
}

void test_jwt_refresh_near_expiry() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_login = [&] {
        return TokenResult{"Bearer initial-token", harness.wall + std::chrono::seconds(30)};
    };
    harness.on_refresh = [&] {
        return TokenResult{"Bearer refreshed-token", harness.wall + std::chrono::hours(1)};
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(result.success, "should succeed after token refresh");
    check(harness.logins == 1, "1 login");
    check(harness.refreshes == 1, "1 refresh near expiry");
}

void test_jwt_reauthentication_on_unauthorized() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_login = [&] {
        if (harness.logins == 1)
            return TokenResult{"Bearer login-token", harness.wall + std::chrono::seconds(20)};
        return TokenResult{"Bearer reauth-token", harness.wall + std::chrono::hours(1)};
    };
    harness.on_refresh = [&]() -> TokenResult {
        throw AuthError(AuthError::Kind::unauthorized, "401 unauthorized", 401);
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(result.success, "should succeed after reauthentication");
    check(harness.logins == 2, "2 logins (initial + reauth)");
    check(harness.refreshes == 1, "1 refresh attempt");
}

void test_governor_budget_state_persistence() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 2;
    config.request_budget.window = std::chrono::seconds(60);
    config.max_attempts = 1;
    auto runtime = harness.make_runtime();
    HttpRequest req;
    run(config, req, runtime);
    check(!harness.persisted_state.empty(), "governor state must be persisted");
    const auto parsed = json::parse(harness.persisted_state);
    check(parsed.at("version") == 2, "version 2 preserves unresolved reservations");
    check(parsed.at("window_seconds") == 60, "window matches");
    check(parsed.at("events").is_array(), "events array");
}

void test_authentication_redirect_reservations() {
    for (const bool insufficient : {false, true}) {
        TestHarness harness;
        auto config = make_test_config(harness.wall);
        config.request_budget.count = insufficient ? 2 : 5;
        harness.on_login = [&] {
            HttpRequest request;
            request.max_redirects = 2;
            harness.before_observer(request);
            HttpTransferInfo info;
            info.http_status = 200;
            info.request_count = 2;
            harness.after_observer(info);
            return TokenResult{"Bearer token", harness.wall + std::chrono::hours(1)};
        };
        auto runtime = harness.make_runtime();
        const auto result = run(config, {}, runtime);
        if (insufficient) {
            check(!result.success && result.reason == "budget_cannot_admit_transfer", "auth must reserve complete redirect allowance");
            check(harness.requests_made.empty(), "insufficient auth budget must prevent registration");
        } else {
            check(result.success, "observed auth transfers can release unused redirect reservation");
            const auto events = json::parse(harness.persisted_state).at("events");
            check(events.size() == 2 && events[0].at("count") == 2 && events[1].at("count") == 1,
                "governor must count auth redirect chain and registration in shared budget");
        }
    }
}

void test_logging_safety() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    // Distinct long digit strings cannot collide with ordinary timestamps,
    // elapsed milliseconds, or counts as the former "001"/"002" checks did.
    config.crns = {"314159265358979323846", "271828182845904523536"};
    harness.on_request = [&](const HttpRequest&) {
        return HttpResponse{200, json{{"ecrnResultList", json::array({
            json{{"crn", config.crns[0]}, {"resultCode", "successResult"}},
            json{{"crn", config.crns[1]}, {"resultCode", "successResult"}}
        })}}.dump(), {}, {}};
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    check(run(config, req, runtime).success, "logging fixture must cover classified successful responses");
    check(!harness.logs.empty(), "logs should be emitted");
    for (const auto& entry : harness.logs) {
        const auto text = entry.dump();
        check(text.find("test-jwt-token") == std::string::npos, "JWT token must not be logged");
        check(text.find("password") == std::string::npos, "password must not be logged");
        for (const auto& crn : config.crns)
            check(text.find(crn) == std::string::npos, "CRN must not be logged in structured events");
    }
}

void test_governor_window_reconfiguration_preserves_events() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 20;
    config.request_budget.window = std::chrono::seconds(1800);
    config.max_attempts = 1;
    // Prior run recorded state with window_seconds = 3600
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(harness.wall.time_since_epoch()).count();
    json previous_state = {
        {"version", 2},
        {"window_seconds", 3600},
        {"last_wall_ms", now_ms},
        {"cooldown_until_ms", 0},
        {"events", json::array({json{{"at_ms", now_ms}, {"count", 1}, {"unresolved", false}}})}
    };
    harness.persisted_state = previous_state.dump();
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(result.success, "window reconfiguration must not reject valid state");
    const auto parsed = json::parse(harness.persisted_state);
    check(parsed.at("window_seconds") == 3600, "shorter active window must preserve known history horizon");
}

void test_authentication_retry_after_persisted() {
    for (const long status : {429L, 503L}) {
        TestHarness harness;
        auto config = make_test_config(harness.wall);
        harness.on_login = [&]() -> TokenResult {
            throw AuthError(AuthError::Kind::http_failure, "synthetic auth failure", status, "120");
        };
        auto runtime = harness.make_runtime();
        const auto start = harness.wall;
        const auto result = run(config, {}, runtime);
        check(!result.success && harness.logins == 1 && harness.requests_made.empty(), "auth failure must stop without registration");
        const auto state = json::parse(harness.persisted_state);
        check(state.at("cooldown_until_ms") == std::chrono::duration_cast<std::chrono::milliseconds>(
            (start + std::chrono::seconds(120)).time_since_epoch()).count(), "auth Retry-After must survive stop/restart");
    }
}

void test_authentication_transport_rate_limit_stops() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_login = []() -> TokenResult {
        throw HttpTransportError(28, 1000, 429, 0, false, false, 0, "90");
    };
    auto runtime = harness.make_runtime();
    const auto result = run(config, {}, runtime);
    check(!result.success && result.reason == "rate_limited", "authentication transport 429 must stop as rate limited");
    check(harness.requests_made.empty(), "authentication transport rate limit must prevent registration");
    const auto state = json::parse(harness.persisted_state);
    check(state.at("cooldown_until_ms") >= std::chrono::duration_cast<std::chrono::milliseconds>(
        (harness.wall + std::chrono::seconds(90)).time_since_epoch()).count(),
        "authentication transport Retry-After must remain persisted");
}

void test_jwt_expiry_during_budget_wait() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 1;
    config.request_budget.window = std::chrono::seconds(60);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(harness.wall.time_since_epoch()).count();
    harness.persisted_state = json{{"version", 2}, {"window_seconds", 60}, {"last_wall_ms", now},
        {"cooldown_until_ms", 0}, {"events", json::array({json{{"at_ms", now}, {"count", 1}, {"unresolved", false}}})}}.dump();
    harness.on_login = [&] { return TokenResult{"Bearer old-token", harness.wall + std::chrono::seconds(40)}; };
    auto runtime = harness.make_runtime();
    const auto result = run(config, {}, runtime);
    check(result.success && result.attempts == 1 && harness.refreshes == 1, "budget wait must renew expired JWT without spending an attempt");
    check(harness.requests_made.size() == 1, "expired JWT must never reach dispatch");
    check(harness.requests_made.front().headers.back() == "Authorization: Bearer test-refreshed-jwt", "dispatch must use renewed JWT");
}

void test_dispatch_guard_after_persistence_and_logging() {
    for (const bool expire_during_log : {false, true}) {
        TestHarness harness;
        auto config = make_test_config(harness.wall);
        config.end = harness.wall + std::chrono::seconds(1);
        auto runtime = harness.make_runtime();
        if (expire_during_log) {
            runtime.log = [&](const json& event) {
                harness.logs.push_back(event);
                if (event.at("event") == "admission") harness.advance(std::chrono::seconds(2));
            };
        } else {
            runtime.write_state = [&, writes = 0](const std::string& state) mutable {
                harness.persisted_state = state;
                if (++writes == 2) harness.advance(std::chrono::seconds(2));
            };
        }
        const auto result = run(config, {}, runtime);
        check(!result.success && result.reason == "window_end", "late admission must stop at window end");
        check(harness.requests_made.empty() && result.attempts == 0, "persistence/log delay must not allow late dispatch");
    }
}

void test_jwt_expiry_during_persistence() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_login = [&] { return TokenResult{"Bearer old-token", harness.wall + std::chrono::seconds(36)}; };
    auto runtime = harness.make_runtime();
    runtime.write_state = [&, writes = 0](const std::string& state) mutable {
        harness.persisted_state = state;
        if (++writes == 2) harness.advance(std::chrono::seconds(2));
    };
    const auto result = run(config, {}, runtime);
    check(result.success && result.attempts == 1 && harness.refreshes == 1, "slow persistence must recheck JWT before dispatch");
    check(harness.requests_made.size() == 1 && harness.requests_made.front().headers.back() ==
        "Authorization: Bearer test-refreshed-jwt", "only refreshed token reaches dispatch");
}

void test_budget_history_retained_and_growth_cooldown() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.end = harness.wall + std::chrono::hours(2);
    config.request_budget.count = 2;
    config.request_budget.window = std::chrono::seconds(1800);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(harness.wall.time_since_epoch()).count();
    harness.persisted_state = json{{"version", 2}, {"window_seconds", 3600}, {"last_wall_ms", now},
        {"cooldown_until_ms", 0}, {"events", json::array({json{{"at_ms", now - 2400000}, {"count", 1}, {"unresolved", false}}})}}.dump();
    auto runtime = harness.make_runtime();
    check(run(config, {}, runtime).success, "smaller window can admit without deleting old retained events");
    check(json::parse(harness.persisted_state).at("events").size() == 2, "old event must survive shrinking window");
    config.request_budget.window = std::chrono::seconds(3600);
    check(run(config, {}, runtime).success, "restored window can admit after original event expires");
    check(harness.sleep_total >= std::chrono::seconds(1200), "restoring window must account for retained old request");

    TestHarness unknown_history;
    config = make_test_config(unknown_history.wall);
    config.end = unknown_history.wall + std::chrono::hours(2);
    unknown_history.persisted_state = json{{"version", 2}, {"window_seconds", 1800}, {"last_wall_ms", now},
        {"cooldown_until_ms", 0}, {"events", json::array()}}.dump();
    runtime = unknown_history.make_runtime();
    check(run(config, {}, runtime).success, "larger window recovers after conservative history cooldown");
    check(unknown_history.sleep_total >= std::chrono::seconds(3599), "window expansion cannot assume discarded history was empty");
}

void test_slow_admission_and_transfer_budget_accounting() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 1;
    config.request_budget.window = std::chrono::seconds(10);
    const auto start = harness.wall;
    std::vector<WallClock::time_point> dispatched;
    harness.on_request = [&](const HttpRequest&) {
        dispatched.push_back(harness.wall);
        if (dispatched.size() == 1) {
            harness.advance(std::chrono::seconds(4));
            return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"VAL06\"},{\"crn\":\"002\",\"resultCode\":\"VAL06\"}]}", {}, {}};
        }
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"successResult\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    runtime.write_state = [&, writes = 0](const std::string& state) mutable {
        harness.persisted_state = state;
        if (++writes == 2) {
            check(json::parse(state).at("events").back().at("unresolved") == true,
                "pre-dispatch reservation must remain unresolved through persistence");
            harness.advance(std::chrono::seconds(15));
        }
    };
    const auto result = run(config, {}, runtime);
    check(result.success && dispatched.size() == 2, "slow persistence/transfer must still allow safe completion");
    check(dispatched[0] >= start + std::chrono::seconds(15), "fixture must delay admission beyond the budget window");
    check(dispatched[1] >= dispatched[0] + std::chrono::seconds(14),
        "next admission must retain the completed transfer throughout its full budget window");
}

void test_authentication_chain_charged_at_completion() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 3;
    config.request_budget.window = std::chrono::seconds(10);
    const auto start = harness.wall;
    harness.on_login = [&] {
        HttpRequest request;
        request.max_redirects = 2;
        harness.before_observer(request);
        harness.advance(std::chrono::seconds(15));
        HttpTransferInfo info;
        info.http_status = 200;
        info.request_count = 3;
        harness.after_observer(info);
        return TokenResult{"Bearer token", harness.wall + std::chrono::hours(1)};
    };
    WallClock::time_point dispatched;
    harness.on_request = [&](const HttpRequest&) {
        dispatched = harness.wall;
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"successResult\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    check(run(config, {}, runtime).success, "long authentication redirect chain must complete within budget");
    check(dispatched >= start + std::chrono::seconds(25), "late auth redirect hops must not age out from initial admission time");
}

void test_unresolved_reservation_recovery() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 1;
    config.request_budget.window = std::chrono::seconds(10);
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        harness.advance(std::chrono::seconds(15));
        throw std::runtime_error("simulate process loss before after-transfer callback");
    };
    auto runtime = harness.make_runtime();
    check(run(config, {}, runtime).reason == "unknown_outcome", "process loss after dispatch must retain unknown-outcome guidance");
    check(json::parse(harness.persisted_state).at("events").back().at("unresolved") == true,
        "unresolved transfer must survive process loss");
    harness.advance(std::chrono::seconds(30));
    const auto restarted = harness.wall;
    harness.on_request = {};
    check(run(config, {}, runtime).success, "restart can recover after a full conservative budget horizon");
    check(harness.wall >= restarted + std::chrono::seconds(10),
        "restart must not expire unresolved transfer from its old admission timestamp");
    check(json::parse(harness.persisted_state).at("events").back().at("unresolved") == false,
        "successful transfer must complete its durable reservation");
}

void test_legacy_budget_state_migration() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 1;
    config.request_budget.window = std::chrono::seconds(60);
    const auto start = harness.wall;
    const auto old = std::chrono::duration_cast<std::chrono::milliseconds>((start - std::chrono::minutes(5)).time_since_epoch()).count();
    harness.persisted_state = json{{"version", 1}, {"window_seconds", 60}, {"last_wall_ms", old},
        {"cooldown_until_ms", 0}, {"events", json::array({json{{"at_ms", old}, {"count", 1}}})}}.dump();
    auto runtime = harness.make_runtime();
    check(run(config, {}, runtime).success, "legacy state must migrate without dropping unknown transfer history");
    check(harness.wall >= start + std::chrono::seconds(60), "legacy migration requires one complete history horizon");
    check(json::parse(harness.persisted_state).at("version") == 2, "migration must persist version 2");
}

void test_clock_health_rechecked_after_budget_wait() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.request_budget.count = 1;
    config.request_budget.window = std::chrono::seconds(60);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(harness.wall.time_since_epoch()).count();
    harness.persisted_state = json{{"version", 2}, {"window_seconds", 60}, {"last_wall_ms", now},
        {"cooldown_until_ms", 0}, {"events", json::array({json{{"at_ms", now}, {"count", 1}, {"unresolved", false}}})}}.dump();
    auto runtime = harness.make_runtime();
    runtime.sleep = [&](std::chrono::milliseconds delay) {
        harness.advance(delay);
        harness.health.synchronized = false;
    };
    const auto result = run(config, {}, runtime);
    check(result.reason == "clock_unsynchronized" && harness.requests_made.empty() && result.attempts == 0,
        "loss of clock synchronization during budget wait must stop before dispatch");
}

void test_normal_interval_starts_after_response_completion() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    std::vector<WallClock::time_point> dispatched;
    harness.on_request = [&](const HttpRequest&) {
        dispatched.push_back(harness.wall);
        if (dispatched.size() == 1) {
            harness.advance(std::chrono::seconds(8));
            return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"VAL06\"},{\"crn\":\"002\",\"resultCode\":\"VAL06\"}]}", {}, {}};
        }
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"successResult\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    check(run(config, {}, runtime).success && dispatched.size() == 2, "slow normal rejection must permit another normal attempt");
    check(dispatched[1] - dispatched[0] >= std::chrono::seconds(11),
        "normal interval must not overlap time spent awaiting the preceding response");
}

void test_long_window_ntp_slewing_tolerated() {
    for (const double speed : {0.9995, 1.0005}) {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.start += std::chrono::seconds(60);
    config.end = harness.wall + std::chrono::hours(2);
    config.min_interval = std::chrono::milliseconds(500000);
    config.max_interval = std::chrono::milliseconds(2000000);
    config.expected_interval = std::chrono::milliseconds(1250000);
    config.max_attempts = 3;
    int attempt_count = 0;
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        ++attempt_count;
        if (attempt_count < 3) {
            return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"VAL06\"},{\"crn\":\"002\",\"resultCode\":\"VAL06\"}]}", {}, {}};
        }
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"successResult\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    runtime.interval = [] { return std::chrono::milliseconds(1250000); };
    // Accumulate more than 1.2s of drift DURING a run, not before Schedule
    // captures its first reference; both forward and backward slew are normal.
    auto base_wall = harness.wall;
    auto base_steady = harness.steady;
    runtime.wall_now = [&]() {
        const auto steady_elapsed = std::chrono::duration<double>(harness.steady - base_steady).count();
        return base_wall + std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(steady_elapsed * speed));
    };
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(result.success, "long window with ntp slewing must succeed without false clock_discontinuity");
    check(result.reason == "all_satisfied", "must complete all satisfied");
    check(harness.sleep_total >= std::chrono::seconds(2560), "test must exercise sustained slew and start-time wait");
    }
}

void test_window_boundary_submillisecond_inflight_expiration() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    config.end = harness.wall + std::chrono::milliseconds(50);
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        harness.advance(std::chrono::milliseconds(100));
        return HttpResponse{200, "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\"},{\"crn\":\"002\",\"resultCode\":\"VAL06\"}]}", {}, {}};
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "in-flight expiration must stop on window_end");
    check(result.reason == "window_end", "reason must be window_end");
    check(result.attempts == 1, "exactly one attempt before window end");
}

void test_ambiguous_disconnect_never_resubmitted() {
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_request = [&](const HttpRequest&) -> HttpResponse {
        throw HttpTransportError(56, 2000, 200, 0, false, false);
    };
    auto runtime = harness.make_runtime();
    HttpRequest req;
    const auto result = run(config, req, runtime);
    check(!result.success, "ambiguous disconnect must fail");
    check(result.reason == "unknown_outcome", "must stop with unknown_outcome");
    check(result.attempts == 1, "never retry ambiguous disconnect");
}

void test_persistence_failure_preserves_dispatch_outcome() {
    for (const bool after_transfer : {false, true}) {
        TestHarness harness;
        auto config = make_test_config(harness.wall);
        auto runtime = harness.make_runtime();
        runtime.write_state = [&, writes = 0](const std::string& state) mutable {
            if (++writes == (after_transfer ? 3 : 2))
                throw std::runtime_error("synthetic durable state write failure");
            harness.persisted_state = state;
        };
        const auto result = run(config, {}, runtime);
        const std::size_t expected_attempts = after_transfer ? 1 : 0;
        check(!result.success && result.reason == (after_transfer ? "unknown_outcome" : "local_failure"),
            "state-write failure must distinguish a possibly applied POST from failure before dispatch");
        check(result.attempts == expected_attempts && harness.requests_made.size() == expected_attempts,
            "storage failure must stop without retrying the registration");
        if (after_transfer)
            check(json::parse(harness.persisted_state).at("events").back().at("unresolved") == true,
                "failed completion persistence must retain the unresolved durable reservation");
    }
    TestHarness harness;
    auto config = make_test_config(harness.wall);
    harness.on_request = [](const HttpRequest&) -> HttpResponse {
        throw HttpTransportError(7, 10, 0, 0, true, false);
    };
    auto runtime = harness.make_runtime();
    runtime.write_state = [&, writes = 0](const std::string& state) mutable {
        if (++writes == 3) throw std::runtime_error("synthetic durable state write failure");
        harness.persisted_state = state;
    };
    const auto result = run(config, {}, runtime);
    check(result.reason == "local_failure" && result.attempts == 1,
        "proven pre-dispatch transport failure remains known even if its accounting fails");
}

void test_observer_failure_preserves_classified_outcome() {
    for (const std::string failure_event : {"attempt", "transfer", "response"}) {
        TestHarness harness;
        auto config = make_test_config(harness.wall);
        auto runtime = harness.make_runtime();
        runtime.log = [&](const json& event) {
            if (event.at("event") == failure_event || event.at("event") == "stop")
                throw std::runtime_error("synthetic diagnostic sink failure");
            harness.logs.push_back(event);
        };
        const auto result = run(config, {}, runtime);
        const std::size_t expected_attempts = failure_event == "attempt" ? 0 : 1;
        check(result.reason == (failure_event == "transfer" ? "unknown_outcome" : "local_failure"),
            "observer failure must preserve dispatch uncertainty until response classification");
        check(result.attempts == expected_attempts && harness.requests_made.size() == expected_attempts,
            "diagnostic failure must not trigger another registration");
    }
}

} // namespace

int main() {
    try {
        test_dry_run();
        test_immediate_all_success();
        test_partial_success_then_completion();
        test_business_rejection_no_backoff();
        test_pre_dispatch_failure_and_backoff();
        test_in_flight_timeout_fails_closed();
        test_truncated_rate_limit_retains_cooldown();
        test_rate_limited_stop();
        test_window_end_stop();
        test_clock_unsynchronized_stop();
        test_clock_discontinuity_stop();
        test_cancellation();
        test_jwt_refresh_near_expiry();
        test_jwt_reauthentication_on_unauthorized();
        test_governor_budget_state_persistence();
        test_authentication_redirect_reservations();
        test_logging_safety();
        test_governor_window_reconfiguration_preserves_events();
        test_authentication_retry_after_persisted();
        test_authentication_transport_rate_limit_stops();
        test_jwt_expiry_during_budget_wait();
        test_dispatch_guard_after_persistence_and_logging();
        test_jwt_expiry_during_persistence();
        test_budget_history_retained_and_growth_cooldown();
        test_slow_admission_and_transfer_budget_accounting();
        test_authentication_chain_charged_at_completion();
        test_unresolved_reservation_recovery();
        test_legacy_budget_state_migration();
        test_clock_health_rechecked_after_budget_wait();
        test_normal_interval_starts_after_response_completion();
        test_long_window_ntp_slewing_tolerated();
        test_window_boundary_submillisecond_inflight_expiration();
        test_ambiguous_disconnect_never_resubmitted();
        test_persistence_failure_preserves_dispatch_outcome();
        test_observer_failure_preserves_classified_outcome();
        std::cout << "All 35 polling execution and orchestration tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
