#define NOMINMAX

#include "clock.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>
#include <limits>
#include <locale>

SystemClock::SystemClock() : offset_ms(0) {}

namespace {
    constexpr int samples = 7;
    constexpr long long http_date_midpoint_ms = 500;
}

std::time_t SystemClock::parse_http_date(const std::wstring& date_str) {
    std::tm tm = {};
    std::wistringstream ss(date_str);
    // Format example: Sat, 07 Feb 2026 14:00:01 GMT
    // Note: Windows implementation of get_time can be locale-sensitive.
    // For robustness in this specific format, simple parsing is often safer, 
    // but get_time works if locale is "C".
    ss.imbue(std::locale("C")); 
    ss >> std::get_time(&tm, L"%a, %d %b %Y %H:%M:%S");
    return _mkgmtime(&tm);
}

void SystemClock::sync_with_server(HINTERNET hConnect) {
    std::cout << "[Clock] Syncing with ITU server..." << std::endl;

    long long best_offset = 0;
    long long best_rtt = std::numeric_limits<long long>::max();
    int successful_samples = 0;

    for (int i = 0; i < samples; i++) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"HEAD", L"/", 
                                                NULL, WINHTTP_NO_REFERER, 
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                                WINHTTP_FLAG_SECURE);

        if (!hRequest) {
            std::cerr << "[Clock] Could not create sync request. Error: " << GetLastError() << std::endl;
            continue;
        }
        
        auto t1 = std::chrono::system_clock::now();
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, NULL)) {
            
            auto t2 = std::chrono::system_clock::now();
            
            wchar_t date_buffer[256];
            DWORD dwSize = sizeof(date_buffer);
            if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Date", date_buffer, &dwSize, WINHTTP_NO_HEADER_INDEX)) {
                std::cerr << "[Clock] Date header missing in sample " << (i + 1) << ". Error: " << GetLastError() << std::endl;
                WinHttpCloseHandle(hRequest);
                continue;
            }

            std::time_t server_time_t = parse_http_date(date_buffer);
            // HTTP Date has only whole-second precision, so estimate the middle of that second.
            auto server_time_pt = std::chrono::system_clock::from_time_t(server_time_t) + std::chrono::milliseconds(http_date_midpoint_ms);
            auto mid_point_local = t1 + (t2 - t1) / 2;
            auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(server_time_pt - mid_point_local).count();
            successful_samples++;
            if (rtt < best_rtt) {
                best_rtt = rtt;
                best_offset = diff;
            }
            
            std::wcout << L"   Sample " << (i + 1) << L": Server Date [" << date_buffer
                       << L"] RTT: " << rtt << L"ms Offset: " << diff << L"ms" << std::endl;
        } else {
            std::cerr << "[Clock] Sync sample failed. Error: " << GetLastError() << std::endl;
        }
        WinHttpCloseHandle(hRequest);
        if (i + 1 < samples) Sleep(300);
    }

    if (successful_samples > 0) {
        this->offset_ms = best_offset;
        std::cout << "[Clock] Selected offset from lowest RTT sample: " << this->offset_ms
                  << "ms (RTT " << best_rtt << "ms; HTTP Date precision is about +/-500ms)" << std::endl;
    } else {
        std::cerr << "[Clock] Could not sync. Falling back to local system time." << std::endl;
        this->offset_ms = 0;
    }
}

void SystemClock::wait_until(int year, int month, int day, int hour, int minute, int second, int millisecond, int lead_millisecond) {
    if (lead_millisecond < 0) lead_millisecond = 0;

    std::tm target_tm = {};
    target_tm.tm_year = year - 1900;
    target_tm.tm_mon  = month - 1;
    target_tm.tm_mday = day;
    target_tm.tm_hour = hour;
    target_tm.tm_min  = minute;
    target_tm.tm_sec  = second;

    auto target_tp = std::chrono::system_clock::from_time_t(std::mktime(&target_tm)) + std::chrono::milliseconds(millisecond);
    auto now_server_estimated = std::chrono::system_clock::now() + std::chrono::milliseconds(offset_ms);
    auto wait_duration = target_tp - now_server_estimated - std::chrono::milliseconds(lead_millisecond);

    if (lead_millisecond > 0) {
        std::cout << "[Clock] Network lead enabled: sending " << lead_millisecond
                  << "ms before the estimated server target time." << std::endl;
    }

    if (wait_duration <= std::chrono::milliseconds(0)) {
        return;
    }

    auto deadline = std::chrono::steady_clock::now() + wait_duration;
    auto last_log = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto remaining = deadline - now;

        if (remaining <= std::chrono::milliseconds(0)) {
            std::cout << "\n[Clock] Target reached." << std::endl;
            return;
        }

        auto ms_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        if (ms_remaining > 250 && now - last_log >= std::chrono::milliseconds(250)) {
            std::cout << "[Clock] Waiting for target time: " << (ms_remaining / 1000.0) << "s   \r" << std::flush;
            last_log = now;
        }

        if (ms_remaining > 2000) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else if (ms_remaining > 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } else if (ms_remaining > 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            YieldProcessor();
        }
    }
}
