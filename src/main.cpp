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

#include "clock.hpp"
#include "token.hpp"
#include "response.hpp"

#include <include/nlohmann_json.hpp>
#include <include/console.hpp>


using json = nlohmann::json;

struct ConfigFlags {
    bool debug;
    bool test;
    bool local;
    bool dry_run;
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
    std::function<void(std::chrono::seconds)> sleep_for = [](auto duration) {
        std::this_thread::sleep_for(duration);
    };
    std::function<void(SystemClock::Wall::time_point)> sleep_until = [](auto target) {
        std::this_thread::sleep_until(target);
    };
    std::function<void(SystemClock&, int, int, int, int, int, int, int, int, TimingQoS*)> final_wait =
        [](SystemClock& clock, int year, int month, int day, int hour, int minute, int second,
           int millisecond, int lead, TimingQoS* qos) {
            clock.wait_until(year, month, day, hour, minute, second, millisecond, lead, qos);
        };
};

// Internal dependency seam for offline fixtures; production supplies fixed OBS endpoints.
int run_application(int argc, char* argv[], HttpSession& session, const AcquireToken& acquire_token,
                    const std::string& origin = "https://obs.itu.edu.tr",
                    const ApplicationSchedule& schedule = {}) {
    itu::platform::ConsoleSession console;
    // Configure program flags
    const ConfigFlags flags = [argc, argv](){
        bool d = false, t = false, l = false, r = false;

        for(int i = 1; i < argc; i++){
            std::string arg = argv[i];
            if(arg == "--logs") d = true;
            if(arg == "--test") t = true;
            if(arg == "--local") l = true;
            if(arg == "--dry-run") r = true;
        }

        return ConfigFlags{d, t, l, r};
    }();

    // Load Configuration
    std::ifstream config_file("data/config.json");
    if (!config_file.is_open()) {
        std::cerr << "[Fatal] data/config.json not found." << std::endl;
        return 1;
    }
    json config;
    config_file >> config;
    std::map<std::string, std::string> env_file = load_env_file(".env");

    // Initialize Helpers
    SystemClock itu_clock;
    // Initial Clock Sync
    if(!flags.local){
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

    if(schedule.wall_now() < sync_tp && !flags.test && !flags.local){
        std::cout << "[System] Wait until 90s..." << std::endl;
        while(schedule.wall_now() < sync_tp){
            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                sync_tp - schedule.wall_now());
            std::cout << "\rRemaining: " << remaining.count() << "s" << std::flush;
            schedule.sleep_for(std::chrono::seconds(1));
        }
        
        std::cout << "[Clock] Re-Sync with ITU Server..." << std::endl;
        itu_clock.sync_with_server(session, origin);
    }
    else if(!flags.local){
        std::cout << "[Warning] Less than 90s remains. Skipping resync..." << std::endl;
    }

    // Wait for Pre-Fetch Phase
    if(!flags.test){
        std::cout << "[System] Waiting until 60s before target for native token acquisition..." << std::endl;
        schedule.sleep_until(token_tp);
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
        std::cerr << "[Fatal] Missing credentials. Add ITU_USERNAME and ITU_PASSWORD to .env or process environment." << std::endl;
        return 1;
    }

    std::string auth_header = acquire_token(
        username,
        password,
        flags.debug // Print extra logs if debug is true
    );

    if (auth_header.empty()) throw std::runtime_error("Authentication returned no token");
    std::cout << "[Success] JWT acquired." << std::endl;
    const auto body_json = registration_payload(config);
    if (flags.debug) std::cout << "[Debug] Prepared " << body_json["ECRN"].size()
                               << " add and " << body_json["SCRN"].size() << " drop entries.\n";
    HttpRequest request;
    request.method = "POST";
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
            schedule.final_wait(
                itu_clock,
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
        }
        timing_qos.activate();
        std::cout << "\n>>> FIRING REGISTRATION REQUEST <<<" << std::endl;
        try {
            response = session.perform();
        } catch (const HttpTransportError&) {
            std::cerr << "[Warning] Registration outcome is unknown. OBS may have applied some or all changes.\n"
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
            [&](const std::string& username, const std::string& password, bool debug) {
                return auth.get_bearer_token(username, password, debug);
            });
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
