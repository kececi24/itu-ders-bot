#include <windows.h>
#include <winhttp.h>
#include <mmsystem.h>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdlib>
#include <map>
#include <cctype>

#include "clock.hpp"
#include "token.hpp"
#include "response.hpp"

#include <include/nlohmann_json.hpp>
#include <include/console.hpp>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "winmm.lib")

using json = nlohmann::json;

struct ConfigFlags {
    bool debug;
    bool test;
    bool local;
    bool dry_run;
};

struct TimerResolutionGuard {
    TimerResolutionGuard() { timeBeginPeriod(1); }
    ~TimerResolutionGuard() { timeEndPeriod(1); }
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
    std::ifstream file(path);
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
    if (const char* value = std::getenv(primary.c_str())) return value;

    auto from_file = env_file.find(primary);
    if (from_file != env_file.end()) return from_file->second;

    if (!legacy.empty()) {
        if (const char* value = std::getenv(legacy.c_str())) return value;

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

int main(int argc, char *argv[]) {
    enable_unicode();
    TimerResolutionGuard timer_resolution;
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

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
    TokenFetcher itu_auth;

    // Setup Persistent Session with Chrome User-Agent
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36", 
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                    WINHTTP_NO_PROXY_NAME, 
                                    WINHTTP_NO_PROXY_BYPASS, 0);

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, L"obs.itu.edu.tr", INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!hConnect) {
        std::cerr << "[Fatal] Could not connect to servers. Error: " << GetLastError() << std::endl;
        return 1;
    }

    // Initial Clock Sync
    if(!flags.local){
        itu_clock.sync_with_server(hConnect);
    }else{
        std::cout << "[Clock] Skipping server synchronization." << std::endl;
    }
    
    // Calculate Target Time Points
    auto t = config["time"];
    int target_second = json_int_with_alias(t, "second", "", 0);
    int target_millisecond = json_int_with_alias(t, "millisecond", "milisecond", 0);
    int lead_millisecond = json_int_with_alias(t, "lead_millisecond", "lead_milisecond", 0);
    std::tm target_tm = {};
    target_tm.tm_year = t["year"].get<int>() - 1900;
    target_tm.tm_mon  = t["month"].get<int>() - 1;
    target_tm.tm_mday = t["day"].get<int>();
    target_tm.tm_hour = t["hour"].get<int>();
    target_tm.tm_min  = t["minute"].get<int>();
    target_tm.tm_sec  = target_second;

    auto target_tp = std::chrono::system_clock::from_time_t(std::mktime(&target_tm));
    auto sync_tp = target_tp - std::chrono::seconds(90);
    auto token_tp = target_tp - std::chrono::seconds(60);

    if(flags.test) std::cout << "[Warning] Test mode enabled, immediately sending request" << std::endl;

    if(std::chrono::system_clock::now() < sync_tp && !flags.test && !flags.local){
        std::cout << "[System] Wait until 90s..." << std::endl;
        while(std::chrono::system_clock::now() < sync_tp){
            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                sync_tp - std::chrono::system_clock::now());
            std::cout << "\rRemaining: " << remaining.count() << "s" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        // std::this_thread::sleep_until(sync_tp);
        
        std::cout << "[Clock] Re-Sync with ITU Server..." << std::endl;
        itu_clock.sync_with_server(hConnect);
    }
    else if(!flags.local){
        std::cout << "[Warning] Less than 90s remains. Skipping resync..." << std::endl;
    }

    // Wait for Pre-Fetch Phase
    if(!flags.test){
        std::cout << "[System] Waiting until 60s before target for native token acquisition..." << std::endl;
        std::this_thread::sleep_until(token_tp);
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
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    std::string auth_header = itu_auth.get_bearer_token(
        username,
        password,
        flags.debug // Print extra logs if debug is true
    );

    if (auth_header.find("ERROR") != std::string::npos) {
        std::cerr << "[Critical] " << auth_header << std::endl;
        return 1;
    }
    std::cout << "[Success] JWT acquired." << std::endl;
    if(flags.debug) std::cout << "[Debug] Auth token: \n" << auth_header << std::endl;

    // Prepare Request Payload (Ensure ECRN/SCRN are arrays)
    std::cout << "[JSON] Preparing add CRN" << std::endl;
    json body_json;
    body_json["ECRN"] = json::array();
    for (auto& item : config["courses"]["crn"]){
        std::cout << "   Adding: " << item.get<std::string>() << std::endl;
        body_json["ECRN"].push_back(item);
    }
    
    std::cout << "[JSON] Preparing drop CRN" << std::endl;
    body_json["SCRN"] = json::array();
    if (config["courses"].contains("scrn")) {
        for (auto& item : config["courses"]["scrn"]){
            std::cout << "   Dropping: " << item.get<std::string>() << std::endl;
            body_json["SCRN"].push_back(item);
        }
    }
    
    std::string body_data = body_json.dump();
    std::wstring wToken(auth_header.begin(), auth_header.end());

    // Build Comprehensive Headers (Browser Fetch)
    std::wstring headers = 
        L"Authorization: " + wToken + L"\r\n" +
        L"Content-Type: application/json\r\n" +
        L"Accept: application/json, text/plain, */*\r\n" +
        L"Accept-Language: tr-TR,tr;q=0.9,en-US;q=0.8,en;q=0.7\r\n" +
        L"Origin: https://obs.itu.edu.tr\r\n" +
        L"Referer: https://obs.itu.edu.tr/ogrenci/DersKayitIslemleri/DersKayit\r\n" +
        L"sec-ch-ua: \"Not(A:Brand\";v=\"8\", \"Chromium\";v=\"144\", \"Google Chrome\";v=\"144\"\r\n" +
        L"sec-ch-ua-mobile: ?0\r\n" +
        L"sec-ch-ua-platform: \"Windows\"\r\n" +
        L"sec-fetch-dest: empty\r\n" +
        L"sec-fetch-mode: cors\r\n" +
        L"sec-fetch-site: same-origin\r\n";

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/ders-kayit/v21",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        std::cerr << "[Fatal] Could not prepare registration request. Error: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    if (flags.dry_run) {
        std::cout << "[DryRun] Login, payload build, and final request preparation succeeded." << std::endl;
        std::cout << "[DryRun] No registration request was sent." << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    // Final Wait
    if(!flags.test) {
        itu_clock.wait_until(
            t["year"].get<int>(),
            t["month"].get<int>(),
            t["day"].get<int>(),
            t["hour"].get<int>(),
            t["minute"].get<int>(),
            target_second,
            target_millisecond,
            lead_millisecond
        );
    }

    // Send registration request
    std::cout << ">>> FIRING REGISTRATION REQUEST <<<" << std::endl;

    if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L, 
                           (LPVOID)body_data.c_str(), (DWORD)body_data.length(), 
                           (DWORD)body_data.length(), 0)) {
        DWORD err = GetLastError();
        std::cerr << "[Error] WinHttpSendRequest failed: " << err << std::endl;

        if(err == ERROR_WINHTTP_CANNOT_CONNECT) std::cerr << "Cannot connect to server." << std::endl;
        if(err == ERROR_WINHTTP_SECURE_FAILURE) std::cerr << "SSL/TLS handshake error." << std::endl;
    } else {
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            std::cerr << "[Error] WinHttpReceiveResponse failed: " << GetLastError() << std::endl;
        } else {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            std::cout << "[Result] Server Response Code: " << code << std::endl;

            std::string response_raw;
            DWORD dwSize = 0;
            do{
                WinHttpQueryDataAvailable(hRequest, &dwSize);
                if(dwSize == 0) break;
                std::vector<char> buffer(dwSize);
                DWORD dwDownloaded = 0;
                WinHttpReadData(hRequest, (LPVOID)&buffer[0], dwSize, &dwDownloaded);
                response_raw.append(buffer.data(), dwDownloaded);
            }while(dwSize > 0);

            if(flags.debug) std::cout << "[Debug] Raw Response: \n" << response_raw << std::endl;

            // Parse response to json
            /** TODO: handle scrn as well */
            try{
                auto res_json = json::parse(response_raw);

                std::cout << "\n--- Registration Results ---" << std::endl;
                for(const auto& item : res_json["ecrnResultList"]){
                    std::string crn = item["crn"].get<std::string>();
                    std::string code = item["resultCode"].get<std::string>();

                    std::cout << get_result_message(code, crn) << std::endl;
                }

            }catch(json::parse_error &e){
                std::cerr << "[Error] Failed to parse response JSON: " << e.what() << std::endl;
                std::cerr << "Raw response: \n" << response_raw << std::endl;
            }
        }
    }

    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    std::cout << "[System] Press Enter to exit." << std::endl;
    std::cin.get();
    return 0;
}
