#include "clock.hpp"
#include <algorithm>
#include <curl/curl.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

void SystemClock::sync_with_server(HttpSession& session, const std::string& origin) {
    sample({[&](const HttpRequest& request) { return session.request(request); },
            [] { return Wall::now(); }, [] { return Steady::now(); },
            [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }}, origin);
}

void SystemClock::sample(const Sampling& sampling, const std::string& origin) {
    std::cout << "[Clock] Syncing with ITU server...\n";
    long long best_rtt = std::numeric_limits<long long>::max();
    long long best_offset = 0;
    int successes = 0;
    for (int index = 0; index < 7; ++index) {
        try {
            HttpRequest request;
            request.method = "HEAD";
            request.url = origin + "/";
            request.timeout_ms = 5000;
            const auto wall_start = sampling.wall_now();
            const auto steady_start = sampling.steady_now();
            const auto response = sampling.request(request);
            const auto elapsed = sampling.steady_now() - steady_start;
            auto date = response.headers.find("date");
            if (response.status < 200 || response.status >= 400 || date == response.headers.end())
                throw std::runtime_error("Clock response unavailable");
            const auto seconds = curl_getdate(date->second.c_str(), nullptr);
            if (seconds == static_cast<std::time_t>(-1)) throw std::runtime_error("Invalid Date header");
            const auto server = Wall::from_time_t(seconds) + std::chrono::milliseconds(500);
            const auto midpoint = wall_start + elapsed / 2;
            const auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            const auto offset = std::chrono::duration_cast<std::chrono::milliseconds>(server - midpoint).count();
            ++successes;
            if (rtt < best_rtt) { best_rtt = rtt; best_offset = offset; }
            std::cout << "   Sample " << index + 1 << ": RTT " << rtt << "ms Offset " << offset << "ms\n";
        } catch (const std::exception&) {
            std::cerr << "[Clock] Sample " << index + 1 << " failed.\n";
        }
        if (index < 6) sampling.sleep(std::chrono::milliseconds(300));
    }
    offset_ms_ = successes ? best_offset : 0;
    if (successes) std::cout << "[Clock] Selected lowest RTT offset: " << offset_ms_
                             << "ms (RTT " << best_rtt << "ms; HTTP Date precision about +/-500ms)\n";
    else std::cerr << "[Clock] Could not sync. Falling back to local system time.\n";
}

SystemClock::Wall::time_point SystemClock::local_target(int year, int month, int day, int hour,
                                                       int minute, int second, int millisecond) {
    std::tm value{};
    value.tm_year = year - 1900; value.tm_mon = month - 1; value.tm_mday = day;
    value.tm_hour = hour; value.tm_min = minute; value.tm_sec = second;
    value.tm_isdst = -1;
    const auto seconds = std::mktime(&value);
    if (seconds == static_cast<std::time_t>(-1)) throw std::runtime_error("Invalid target time");
    return Wall::from_time_t(seconds) + std::chrono::milliseconds(millisecond);
}

SystemClock::Steady::time_point SystemClock::deadline(Wall::time_point target, Wall::time_point wall_now,
                                                      Steady::time_point steady_now, long long offset_ms, int lead_ms) {
    return steady_now + std::chrono::duration_cast<Steady::duration>(
        target - wall_now - std::chrono::milliseconds(offset_ms) - std::chrono::milliseconds(std::max(0, lead_ms)));
}

void SystemClock::wait_until(int year, int month, int day, int hour, int minute, int second,
                            int millisecond, int lead_millisecond, TimingQoS* qos) {
    wait_until_with({[] { return Wall::now(); }, [] { return Steady::now(); },
                     [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); },
                     [] { itu::platform::cpu_relax(); }},
                    year, month, day, hour, minute, second, millisecond, lead_millisecond, qos);
}

void SystemClock::wait_until_with(const Waiting& waiting, int year, int month, int day, int hour,
                                 int minute, int second, int millisecond, int lead_millisecond, TimingQoS* qos) {
    const auto target = local_target(year, month, day, hour, minute, second, millisecond);
    const auto wall_now = waiting.wall_now();
    const auto steady_now = waiting.steady_now();
    const auto end = deadline(target, wall_now, steady_now, offset_ms_, lead_millisecond);
    if (lead_millisecond > 0) std::cout << "[Clock] Network lead: " << lead_millisecond << "ms.\n";
    auto last_log = steady_now - std::chrono::seconds(1);
    while (true) {
        const auto now = waiting.steady_now();
        const auto remaining = end - now;
        if (remaining <= std::chrono::seconds(2) && qos) qos->activate();
        if (remaining <= Steady::duration::zero()) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        if (ms > 250 && now - last_log >= std::chrono::milliseconds(250)) {
            std::cout << "[Clock] Waiting for target time: " << ms / 1000.0 << "s   \r" << std::flush;
            last_log = now;
        }
        if (ms > 2000) waiting.sleep(std::chrono::seconds(1));
        else if (ms > 100) waiting.sleep(std::chrono::milliseconds(20));
        else if (ms > 5) waiting.sleep(std::chrono::milliseconds(1));
        else waiting.spin();
    }
}
