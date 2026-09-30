#pragma once
#include "http.hpp"
#include <include/platform.hpp>
#include <chrono>
#include <ctime>
#include <functional>
#include <string>

// Platform scheduling preferences are best effort and scoped through submission.
using TimingQoS = itu::platform::TimingGuard;

class SystemClock {
public:
    using Wall = std::chrono::system_clock;
    using Steady = std::chrono::steady_clock;
    struct Sampling {
        std::function<HttpResponse(const HttpRequest&)> request;
        std::function<Wall::time_point()> wall_now;
        std::function<Steady::time_point()> steady_now;
        std::function<void(std::chrono::milliseconds)> sleep;
    };
    struct Waiting {
        std::function<Wall::time_point()> wall_now;
        std::function<Steady::time_point()> steady_now;
        std::function<void(std::chrono::milliseconds)> sleep;
        std::function<void()> spin;
    };
    void wait_until_with(const Waiting& waiting, int year, int month, int day, int hour,
                         int minute, int second, int millisecond, int lead_millisecond, TimingQoS* qos = nullptr);
    void sync_with_server(HttpSession& session, const std::string& origin = "https://obs.itu.edu.tr");
    void sample(const Sampling& sampling, const std::string& origin);
    static Wall::time_point local_target(int year, int month, int day, int hour, int minute,
                                         int second = 0, int millisecond = 0);
    static Steady::time_point deadline(Wall::time_point target, Wall::time_point wall_now,
                                       Steady::time_point steady_now, long long offset_ms, int lead_ms);
    void wait_until(int year, int month, int day, int hour, int minute, int second = 0,
                    int millisecond = 0, int lead_millisecond = 0, TimingQoS* qos = nullptr);
    long long get_offset() const { return offset_ms_; }
private:
    long long offset_ms_ = 0;
};
