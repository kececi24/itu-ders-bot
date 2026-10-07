#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdlib>
#include <map>
#include <cctype>
#include <functional>
#include <filesystem>
#include <algorithm>
#include <cmath>

#include <type_traits>

#include "clock.hpp"
#include "token.hpp"
#include "response.hpp"
#include "polling.hpp"
#include "registration_result.hpp"

#include <include/polling_config.hpp>
#include <include/nlohmann_json.hpp>
#include <include/console.hpp>


using json = nlohmann::json;

struct ConfigFlags {
    bool debug = false;
    bool test = false;
    bool local = false;
    bool dry_run = false;
    bool server_time = false;
    bool check_clock = false;
};

std::string trim_copy(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) first++;

    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) last--;

    return value.substr(first, last - first);
}

std::string unquote_copy(const std::string& value) {
    if (value.size() >= 2) {
        char first = value.front();
        char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }

    return value;
}

std::map<std::string, std::string> load_env_file(const std::string& path) {
    std::map<std::string, std::string> values;
    std::ifstream file(std::filesystem::u8path(path));
    if (!file.is_open()) return values;

    std::string line;
    while (std::getline(file, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') continue;

        const std::string export_prefix = "export ";
        if (line.rfind(export_prefix, 0) == 0) {
            line = trim_copy(line.substr(export_prefix.size()));
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim_copy(line.substr(0, eq));
        std::string value = unquote_copy(trim_copy(line.substr(eq + 1)));
        if (!key.empty()) values[key] = value;
    }

    return values;
}

std::string get_secret_value(const std::map<std::string, std::string>& env_file, const std::string& primary, const std::string& legacy) {
    if (auto value = itu::platform::environment(primary)) return *value;

    auto from_file = env_file.find(primary);
    if (from_file != env_file.end()) return from_file->second;

    if (!legacy.empty()) {
        if (auto value = itu::platform::environment(legacy)) return *value;

        auto legacy_from_file = env_file.find(legacy);
        if (legacy_from_file != env_file.end()) return legacy_from_file->second;
    }

    return "";
}

int json_int_with_alias(const json& obj, const std::string& primary, const std::string& alias, int fallback) {
    if (obj.contains(primary)) return obj[primary].get<int>();
    if (!alias.empty() && obj.contains(alias)) return obj[alias].get<int>();
    return fallback;
}

using AcquireToken = std::function<std::string(const std::string&, const std::string&, bool)>;

struct TokenProvider {
    std::function<std::string(const std::string&, const std::string&, bool)> legacy;
    std::function<TokenResult(const std::string&, const std::string&, bool)> login;
    std::function<TokenResult(bool)> refresh;
    std::function<void(HttpSession::BeforeTransfer, HttpSession::AfterTransfer)> observers;
    std::function<void(std::function<bool()>)> set_cancelled;

    template <typename F, typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<F>, TokenProvider> &&
        !std::is_same_v<std::decay_t<F>, TokenFetcher>>>
    TokenProvider(F&& fn) {
        AcquireToken callable(std::forward<F>(fn));
        legacy = callable;
        login = [callable](const std::string& u, const std::string& p, bool d) -> TokenResult {
            return TokenResult{callable(u, p, d), std::nullopt};
        };
    }
    TokenProvider(TokenFetcher& fetcher) {
        legacy = [&fetcher](const std::string& u, const std::string& p, bool d) {
            return fetcher.get_bearer_token(u, p, d);
        };
        login = [&fetcher](const std::string& u, const std::string& p, bool d) {
            return fetcher.get_token(u, p, d);
        };
        refresh = [&fetcher](bool d) {
            return fetcher.refresh_token(d);
        };
        observers = [&fetcher](HttpSession::BeforeTransfer b, HttpSession::AfterTransfer a) {
            fetcher.set_observer(std::move(b), std::move(a));
        };
        set_cancelled = [&fetcher](std::function<bool()> c) {
            fetcher.set_cancelled(std::move(c));
        };
    }
};

json registration_payload(const json& config) {
    json payload = {{"ECRN", json::array()}, {"SCRN", json::array()}};
    const auto& courses = config.at("courses");
    for (const auto* key : {"crn", "scrn"}) {
        if (!courses.contains(key) && std::string(key) == "scrn") continue;
        const auto& values = courses.at(key);
        if (!values.is_array()) throw std::runtime_error("Course lists must be arrays");
        for (const auto& value : values) {
            if (!value.is_string()) throw std::runtime_error("CRNs must be strings");
            payload[std::string(key) == "crn" ? "ECRN" : "SCRN"].push_back(value);
        }
    }
    return payload;
}

// Scheduling callbacks keep offline orchestration tests deterministic. Production
// uses the same real clock and sleep operations as the original flow.
struct ApplicationSchedule {
    std::function<SystemClock::Wall::time_point()> wall_now = [] { return SystemClock::Wall::now(); };
    std::function<SystemClock::Steady::time_point()> steady_now = [] { return SystemClock::Steady::now(); };
    std::function<void(std::chrono::milliseconds)> sleep_ms = [](auto duration) {
        std::this_thread::sleep_for(duration);
    };
    std::function<void()> spin = [] { itu::platform::cpu_relax(); };
    std::function<itu::platform::ClockHealth()> clock_health = [] { return itu::platform::clock_health(); };
    std::function<bool()> cancelled;
    std::function<void(const char*, SystemClock::Wall::time_point)> waiting;
};
struct ApplicationStop : std::runtime_error {
    const char* reason;
    ApplicationStop(const char* reason_, const char* message) : std::runtime_error(message), reason(reason_) {}
};

// Internal dependency seam for offline fixtures; production supplies fixed OBS endpoints.
int run_application(int argc, char* argv[], HttpSession& session, const TokenProvider& provider,
                    const std::string& origin = "https://obs.itu.edu.tr",
                    const ApplicationSchedule& schedule = {}) {
    itu::platform::ConsoleSession console;
    // Configure program flags
    const ConfigFlags flags = [argc, argv](){
        bool d = false, t = false, l = false, r = false, s = false, c = false;

        for(int i = 1; i < argc; i++){
            std::string arg = argv[i];
            if(arg == "--logs") d = true;
            if(arg == "--test") t = true;
            if(arg == "--local") l = true;
            if(arg == "--dry-run") r = true;
            if(arg == "--server-time") s = true;
            if(arg == "--check-clock") c = true;
        }

        return ConfigFlags{d, t, l, r, s, c};
    }();

    const auto started = schedule.steady_now();
    bool polling_enabled = false;
    auto failure = [&](const char* reason, const char* message) {
        if (flags.debug) {
            std::cerr << json{{"event", "stop"}, {"reason", reason}, {"attempt", 0},
                {"timestamp_ms", std::chrono::duration_cast<std::chrono::milliseconds>(schedule.wall_now().time_since_epoch()).count()},
                {"elapsed_ms", std::chrono::duration_cast<std::chrono::milliseconds>(schedule.steady_now() - started).count()},
                {"polling", polling_enabled}, {"dry_run", flags.dry_run}, {"test", flags.test},
                {"local", !flags.server_time}, {"server_time", flags.server_time}}.dump() << '\n';
            std::cout << "[Fatal] " << message << '\n';
        } else std::cerr << "[Fatal] " << message << '\n';
        return 1;
    };
    try {
    if (flags.check_clock) {
        const auto health = schedule.clock_health();
        const auto report = itu::polling::clock_report(health);
        if (flags.debug) {
            std::cerr << report.dump() << "\n";
        }
        std::cout << "[Clock Diagnostics]\n";
        std::cout << "  Provider: " << health.provider << "\n";
        std::cout << "  Status:   " << health.reason << "\n";
        if (health.network_time_enabled)
            std::cout << "  Network Time Enabled: " << (*health.network_time_enabled ? "yes" : "no") << "\n";
        if (health.synchronized)
            std::cout << "  Synchronized:         " << (*health.synchronized ? "yes" : "no") << "\n";
        if (health.max_error_us)
            std::cout << "  Max Error:            " << *health.max_error_us << " us\n";
        if (health.estimated_error_us)
            std::cout << "  Estimated Error:      " << *health.estimated_error_us << " us\n";
        return (health.synchronized && !*health.synchronized) ? 1 : 0;
    }

    // Load Configuration
    std::ifstream config_file("data/config.json");
    if (!config_file.is_open()) {
        return failure("configuration_missing", "data/config.json not found.");
    }
    json config;
    config_file >> config;
    std::map<std::string, std::string> env_file = load_env_file(".env");

    itu::polling::Config polling_cfg;
    if (config.contains("polling")) {
        try {
            polling_cfg = itu::polling::parse(config);
            polling_enabled = polling_cfg.enabled;
        } catch (const std::exception&) {
            return failure("invalid_polling_configuration", "Invalid polling configuration; check the documented constraints.");
        }
    }

    if (polling_enabled) {
        if (flags.server_time) {
            return failure("invalid_polling_mode", "--server-time cannot be used with polling registration.");
        }
        if (flags.test && !flags.dry_run) {
            return failure("invalid_polling_mode", "Submission-enabled --test cannot be used with polling registration.");
        }
        std::string username = get_secret_value(env_file, "ITU_USERNAME", "ITU_OBS_USERNAME");
        std::string password = get_secret_value(env_file, "ITU_PASSWORD", "ITU_OBS_PASSWORD");

        if (username.empty() && config.contains("account") && config["account"].contains("username")) {
            username = config["account"]["username"].get<std::string>();
        }
        if (password.empty() && config.contains("account") && config["account"].contains("password")) {
            password = config["account"]["password"].get<std::string>();
        }

        if (username.empty() || password.empty()) {
            return failure("credentials_missing", "Missing credentials. Add ITU_USERNAME and ITU_PASSWORD to .env or process environment.");
        }

        const auto runtime_dir = std::filesystem::u8path(".itu-runtime");
        itu::platform::PollingStorage storage(runtime_dir);
        itu::platform::Cancellation cancellation;
        // Sessions outlive this invocation. Never leave callbacks referring to
        // its destroyed cancellation guard or scheduler/governor state.
        struct ResetCallbacks {
            HttpSession& session;
            const TokenProvider& provider;
            ~ResetCallbacks() {
                session.set_observer({}, {});
                session.set_cancelled({});
                if (provider.observers) provider.observers({}, {});
                if (provider.set_cancelled) provider.set_cancelled({});
            }
        } reset{session, provider};

        itu::polling::Runtime runtime;
        runtime.wall_now = [&] { return schedule.wall_now(); };
        runtime.steady_now = schedule.steady_now;
        runtime.sleep = [&](std::chrono::milliseconds ms) {
            schedule.sleep_ms(ms);
        };
        runtime.cancelled = [&] { return cancellation.cancelled() || (schedule.cancelled && schedule.cancelled()); };
        runtime.clock_health = schedule.clock_health;
        runtime.read_state = [&] { return storage.read(); };
        runtime.write_state = [&](const std::string& s) { storage.write(s); };
        runtime.login = [&]() -> TokenResult {
            if (provider.login) return provider.login(username, password, flags.debug);
            return TokenResult{provider.legacy(username, password, flags.debug), std::nullopt};
        };
        runtime.refresh = [&]() -> TokenResult {
            if (provider.refresh) return provider.refresh(flags.debug);
            throw AuthError(AuthError::Kind::login_required, "Refresh not supported");
        };
        // The session-scoped callback below covers these transfers. Do not
        // leave a second closure in its retained prepared request after return.
        runtime.request = [&](HttpRequest req) { req.cancelled = {}; return session.request(req); };
        runtime.prepare = [&](HttpRequest req) { req.cancelled = {}; session.prepare(req); };
        session.set_cancelled(runtime.cancelled);
        if (provider.set_cancelled) provider.set_cancelled(runtime.cancelled);
        runtime.observers = [&](HttpSession::BeforeTransfer before, HttpSession::AfterTransfer after) {
            session.set_observer(before, after);
            if (provider.observers) provider.observers(before, after);
        };
        runtime.log = [&](const nlohmann::json& evt) {
            if (flags.debug) {
                auto structured = evt;
                structured["dry_run"] = flags.dry_run;
                structured["test"] = flags.test;
                structured["local"] = true;
                std::cerr << structured.dump() << "\n";
            }
            const std::string event_type = evt.value("event", "");
            if (event_type == "attempt") {
                std::cout << "[Polling] Attempt " << evt.value("attempt", 0) << " dispatching ("
                          << evt.value("pending_count", 0) << " pending)..." << std::endl;
            } else if (event_type == "jwt_acquired") {
                std::cout << "[Success] JWT acquired." << std::endl;
            } else if (event_type == "jwt_refresh") {
                std::cout << "[Clock] Refreshing JWT token..." << std::endl;
            } else if (event_type == "jwt_reauthentication") {
                std::cout << "[Clock] Re-authenticating session..." << std::endl;
            } else if (event_type == "backoff") {
                std::cout << "[Warning] Transport failure; backing off "
                          << evt.value("next_interval_ms", 0) << " ms..." << std::endl;
            } else if (event_type == "interval") {
                std::cout << "[Polling] Waiting " << evt.value("next_interval_ms", 0)
                          << " ms until next attempt..." << std::endl;
            } else if (event_type == "response") {
                std::cout << "[Polling] Response: " << evt.value("category", "unknown") << '\n';
                if (evt.contains("codes")) for (const auto& code : evt.at("codes")) {
                    const auto value = code.get<std::string>();
                    // Only display allowlisted messages; the CRN list and raw
                    // server text never enter structured or console diagnostics.
                    if (RESULT_MESSAGES.count(value))
                        std::cout << "  " << value << ": " << get_result_message(value, "[requested course]") << '\n';
                }
            } else if (event_type == "clock" && evt.contains("synchronized") && evt.at("synchronized").is_null()) {
                std::cout << "[Warning] Local clock synchronization could not be established; check system network-time settings.\n";
            }
        };

        HttpRequest registration_request;
        registration_request.url = origin + "/api/ders-kayit/v21";
        registration_request.headers = {
            "Content-Type: application/json",
            "Accept: application/json, text/plain, */*",
            "Accept-Language: tr-TR,tr;q=0.9,en-US;q=0.8,en;q=0.7",
            "Origin: " + origin,
            "Referer: " + origin + "/ogrenci/DersKayitIslemleri/DersKayit",
            "sec-ch-ua: \"Not(A:Brand\";v=\"8\", \"Chromium\";v=\"144\", \"Google Chrome\";v=\"144\"",
            "sec-ch-ua-mobile: ?0",
            std::string("sec-ch-ua-platform: ") + itu::platform::browser_platform(),
            "sec-fetch-dest: empty",
            "sec-fetch-mode: cors",
            "sec-fetch-site: same-origin"
        };
        itu::polling::Options options;
        options.dry_run = flags.dry_run;
        options.test = flags.test;

        std::cout << "[Polling] Starting add-only registration polling...\n";
        std::cout << "  Distribution: " << (polling_cfg.distribution == itu::polling::Distribution::beta ? "beta" : "uniform") << "\n";
        std::cout << "  Interval:     min " << polling_cfg.min_interval.count() << " ms, max " << polling_cfg.max_interval.count() << " ms\n";
        std::cout << "  Nominal Mean: " << polling_cfg.expected_interval.count() << " ms (~"
                  << static_cast<long long>(3600000.0 / polling_cfg.expected_interval.count()) << " requests/hour)\n";
        std::cout << "  Budget:       " << polling_cfg.request_budget.count << " requests per "
                  << polling_cfg.request_budget.window.count() << " seconds\n";
        std::cout << "  Max Attempts: " << polling_cfg.max_attempts << "\n";
        if (options.dry_run) {
            std::cout << "[DryRun] Polling dry-run mode enabled; zero registration POSTs will be sent.\n";
        }

        const auto result = itu::polling::run(polling_cfg, registration_request, runtime, options);
        if (result.success) {
            if (flags.dry_run) std::cout << "[DryRun] Login, payload build, and final request preparation succeeded. No registration request was sent.\n";
            else std::cout << "[Success] Registration completed: " << result.reason
                           << " (attempts: " << result.attempts << ")\n";
            if (itu::platform::is_terminal()) {
                std::cout << "[System] Press Enter to exit." << std::endl;
                std::cin.get();
            }
            return 0;
        }
        (flags.debug ? std::cout : std::cerr) << "[Fatal] Polling stopped: " << result.reason
                  << " (attempts: " << result.attempts << ")\n";
        if (result.reason == "unknown_outcome" || result.reason == "interrupted_unknown_outcome")
            std::cout << "[Warning] Registration outcome is unknown. Check your registered courses in OBS before retrying.\n";
        if (result.reason == "action_required")
            std::cout << "[Action] Review the reported course conditions in OBS and correct them before restarting.\n";
        return 1;
    }

    itu::platform::Cancellation cancellation;
    struct ResetSingleCallbacks {
        HttpSession& session;
        const TokenProvider& provider;
        ~ResetSingleCallbacks() {
            session.set_observer({}, {});
            session.set_cancelled({});
            if (provider.set_cancelled) provider.set_cancelled({});
        }
    } reset_single{session, provider};
    const auto cancelled = [&] { return cancellation.cancelled() || (schedule.cancelled && schedule.cancelled()); };
    session.set_cancelled(cancelled);
    if (provider.set_cancelled) provider.set_cancelled(cancelled);
    auto previous_wall = schedule.wall_now();
    auto previous_steady = schedule.steady_now();
    auto check_timing = [&] {
        if (cancelled()) throw ApplicationStop("interrupted", "Interrupted before registration dispatch.");
        const auto wall = schedule.wall_now();
        const auto steady = schedule.steady_now();
        const auto wall_delta = std::chrono::duration<long double>(wall - previous_wall).count();
        const auto steady_delta = std::chrono::duration<long double>(steady - previous_steady).count();
        if (steady_delta < 0 || std::abs(wall_delta - steady_delta) > 1.0L)
            throw ApplicationStop("clock_discontinuity", "Clock discontinuity detected; registration was not dispatched.");
        previous_wall = wall;
        previous_steady = steady;
    };
    bool warned_unknown = false;
    auto check_health = [&] {
        check_timing();
        const auto health = schedule.clock_health();
        // Legacy HTTP-Date sampling can fall back to local time and cannot
        // establish NTP health. A positive unsynchronized report still stops.
        if (health.synchronized && !*health.synchronized)
            throw ApplicationStop("clock_unsynchronized", "Local clock is reported unsynchronized; registration was not dispatched.");
        if (!flags.server_time && !health.synchronized && !warned_unknown) {
            std::cout << "[Warning] Local clock synchronization could not be established; check system network-time settings.\n";
            warned_unknown = true;
        }
        check_timing();
    };
    auto wait_until = [&](const char* stage, SystemClock::Wall::time_point target) {
        check_timing();
        if (schedule.waiting) schedule.waiting(stage, target);
        while (true) {
            check_timing();
            const auto delay = target - schedule.wall_now();
            if (delay <= SystemClock::Wall::duration::zero()) break;
            const auto deadline = schedule.steady_now() + std::chrono::duration_cast<SystemClock::Steady::duration>(delay);
            while (schedule.steady_now() < deadline) {
                check_timing();
                schedule.sleep_ms(std::min(std::chrono::ceil<std::chrono::milliseconds>(deadline - schedule.steady_now()),
                                          std::chrono::milliseconds(100)));
            }
            // A small backward NTP correction must not cause early dispatch.
        }
        check_timing();
    };
    check_health();
    // Initialize Helpers
    SystemClock itu_clock;
    // Initial Clock Sync
    if(flags.server_time){
        std::cout << "[Clock] Synchronizing with ITU Server via HTTP-Date..." << std::endl;
        itu_clock.sync_with_server(session, origin);
    }else{
        std::cout << "[Clock] Skipping server synchronization." << std::endl;
    }
    
    // Calculate Target Time Points
    auto t = config["time"];
    int target_second = json_int_with_alias(t, "second", "", 0);
    int target_millisecond = json_int_with_alias(t, "millisecond", "milisecond", 0);
    int lead_millisecond = json_int_with_alias(t, "lead_millisecond", "lead_milisecond", 0);
    auto target_tp = SystemClock::local_target(t["year"].get<int>(), t["month"].get<int>(),
        t["day"].get<int>(), t["hour"].get<int>(), t["minute"].get<int>(), target_second);
    auto sync_tp = target_tp - std::chrono::seconds(90);
    auto token_tp = target_tp - std::chrono::seconds(60);

    if (flags.test) std::cout << (flags.dry_run ? "[Test] Scheduled waits disabled; dry-run prevents submission." : "[Warning] Test mode sends immediately.") << std::endl;

    if(schedule.wall_now() < sync_tp && !flags.test && flags.server_time){
        std::cout << "[System] Wait until 90s..." << std::endl;
        wait_until("resync_wait", sync_tp);
        
        std::cout << "[Clock] Re-Sync with ITU Server..." << std::endl;
        itu_clock.sync_with_server(session, origin);
    }
    else if(flags.server_time){
        std::cout << "[Warning] Less than 90s remains. Skipping resync..." << std::endl;
    }

    // Wait for Pre-Fetch Phase
    if(!flags.test){
        std::cout << "[System] Waiting until 60s before target for native token acquisition..." << std::endl;
        wait_until("token_wait", token_tp);
    }

    // Acquire Token
    std::string username = get_secret_value(env_file, "ITU_USERNAME", "ITU_OBS_USERNAME");
    std::string password = get_secret_value(env_file, "ITU_PASSWORD", "ITU_OBS_PASSWORD");

    if (username.empty() && config.contains("account") && config["account"].contains("username")) {
        username = config["account"]["username"].get<std::string>();
    }
    if (password.empty() && config.contains("account") && config["account"].contains("password")) {
        password = config["account"]["password"].get<std::string>();
    }

    if (username.empty() || password.empty()) {
        return failure("credentials_missing", "Missing credentials. Add ITU_USERNAME and ITU_PASSWORD to .env or process environment.");
    }

    std::string auth_header = provider.legacy ? provider.legacy(username, password, flags.debug)
        : provider.login(username, password, flags.debug).bearer;

    if (auth_header.empty()) throw std::runtime_error("Authentication returned no token");
    std::cout << "[Success] JWT acquired." << std::endl;
    const auto body_json = registration_payload(config);
    if (flags.debug) std::cout << "[Debug] Prepared " << body_json["ECRN"].size()
                               << " add and " << body_json["SCRN"].size() << " drop entries.\n";
    HttpRequest request;
    request.method = "POST";
    request.follow_redirects = false;
    request.max_redirects = 0;
    request.url = origin + "/api/ders-kayit/v21";
    request.body = body_json.dump();
    request.headers = {
        "Authorization: " + auth_header,
        "Content-Type: application/json",
        "Accept: application/json, text/plain, */*",
        "Accept-Language: tr-TR,tr;q=0.9,en-US;q=0.8,en;q=0.7",
        "Origin: " + origin,
        "Referer: " + origin + "/ogrenci/DersKayitIslemleri/DersKayit",
        "sec-ch-ua: \"Not(A:Brand\";v=\"8\", \"Chromium\";v=\"144\", \"Google Chrome\";v=\"144\"",
        "sec-ch-ua-mobile: ?0",
        std::string("sec-ch-ua-platform: ") + itu::platform::browser_platform(),
        "sec-fetch-dest: empty",
        "sec-fetch-mode: cors",
        "sec-fetch-site: same-origin"
    };
    session.prepare(request);

    if (flags.dry_run) {
        check_health();
        std::cout << "[DryRun] Login, payload build, and final request preparation succeeded." << std::endl;
        std::cout << "[DryRun] No registration request was sent." << std::endl;
        return 0;
    }

    HttpResponse response;
    {
        TimingQoS timing_qos;
        // Keep the elevated QoS through submission, then restore it before
        // parsing/displaying the result or waiting for interactive exit.
        if(!flags.test) {
            if (schedule.waiting) schedule.waiting("final_wait", target_tp + std::chrono::milliseconds(target_millisecond - lead_millisecond));
            SystemClock::Waiting waiting;
            waiting.wall_now = schedule.wall_now;
            waiting.steady_now = [&] { check_timing(); return schedule.steady_now(); };
            waiting.sleep = [&](auto delay) { schedule.sleep_ms(std::min(delay, std::chrono::milliseconds(100))); };
            waiting.spin = schedule.spin;
            do {
                itu_clock.wait_until_with(
                    waiting,
                    t["year"].get<int>(),
                    t["month"].get<int>(),
                    t["day"].get<int>(),
                    t["hour"].get<int>(),
                    t["minute"].get<int>(),
                    target_second,
                    target_millisecond,
                    lead_millisecond,
                    &timing_qos
                );
            } while (!flags.server_time && schedule.wall_now() <
                target_tp + std::chrono::milliseconds(target_millisecond - std::max(0, lead_millisecond)));
        }
        check_health();
        timing_qos.activate();
        std::cout << "\n>>> FIRING REGISTRATION REQUEST <<<" << std::endl;
        // This runs after preparation/output, immediately before curl dispatch.
        session.set_observer([&](const HttpRequest&) { check_health(); }, {});
        try {
            response = session.perform();
        } catch (const HttpTransportError&) {
            (flags.debug ? std::cout : std::cerr) << "[Warning] Registration outcome is unknown. OBS may have applied some or all changes.\n"
                      << "Check your registered courses in OBS before retrying. No automatic retry was attempted.\n";
            throw;
        }
    }
    std::cout << "[Result] Server Response Code: " << response.status << std::endl;
    if (response.status < 200 || response.status >= 300)
        throw std::runtime_error("Registration HTTP request failed");
    // Drop-result schema remains undocumented; retain the existing add-result display.
    const auto results = json::parse(response.body);
    if (!results.is_object() || !results.contains("ecrnResultList") || !results["ecrnResultList"].is_array())
        throw std::runtime_error("Invalid registration response");
    std::cout << "\n--- Registration Results ---" << std::endl;
    for (const auto& item : results["ecrnResultList"]) {
        const auto crn = item.at("crn").get<std::string>();
        const auto code = item.at("resultCode").get<std::string>();
        std::cout << get_result_message(code, crn) << std::endl;
    }
    if (itu::platform::is_terminal()) {
        std::cout << "[System] Press Enter to exit." << std::endl;
        std::cin.get();
    }
    return 0;
    } catch (const ApplicationStop& error) {
        return failure(error.reason, error.what());
    } catch (const HttpTransportError& error) {
        // Typed transport messages contain fixed text and numeric diagnostics.
        return failure("transport_failure", error.what());
    } catch (const json::exception&) {
        return failure("invalid_json", "Invalid configuration or registration response JSON.");
    } catch (const std::exception&) {
        return failure("local_failure", "Request or application operation failed. No automatic retry was attempted.");
    }
}

#ifndef ITU_NO_MAIN
int main(int argc, char* argv[]) {
    try {
        auto arguments = itu::platform::arguments(argc, argv);
        std::vector<char*> utf8_argv;
        for (auto& argument : arguments) utf8_argv.push_back(argument.data());
        utf8_argv.push_back(nullptr);
        HttpSession session;
        TokenFetcher auth;
        return run_application(static_cast<int>(arguments.size()), utf8_argv.data(), session,
            TokenProvider(auth));
    } catch (const json::exception&) {
        std::cerr << "[Fatal] Invalid configuration or registration response JSON.\n";
    } catch (const std::runtime_error& error) {
        // Transport/authentication runtime errors contain only fixed text and status codes.
        std::cerr << "[Fatal] " << error.what() << "\n";
    } catch (const std::exception&) {
        // Exception text can include secrets from malformed configuration/response data.
        std::cerr << "[Fatal] Request or application operation failed. No automatic retry was attempted.\n";
    }
    return 1;
}
#endif
