#pragma once
#include "http.hpp"
#include "token.hpp"
#include "include/platform.hpp"
#include "include/polling_config.hpp"
#include <functional>
#include <optional>
#include <random>

namespace itu::polling {
using SteadyClock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
using Json = nlohmann::json;

// Runtime injection is also used by deterministic offline orchestration tests.
// Production binds these to native clocks, private storage and persistent sessions.
struct Runtime {
    std::function<WallClock::time_point()> wall_now = [] { return WallClock::now(); };
    std::function<SteadyClock::time_point()> steady_now = [] { return SteadyClock::now(); };
    std::function<void(Milliseconds)> sleep;
    std::function<bool()> cancelled;
    std::function<platform::ClockHealth()> clock_health;
    std::function<std::optional<std::string>()> read_state;
    std::function<void(const std::string&)> write_state;
    std::function<TokenResult()> login, refresh;
    std::function<void(const HttpRequest&)> prepare;
    std::function<HttpResponse(const HttpRequest&)> request;
    std::function<void(HttpSession::BeforeTransfer, HttpSession::AfterTransfer)> observers;
    std::function<void(const Json&)> log;
    // Empty in production: a process-seeded independent sampler is used.
    std::function<Milliseconds()> interval;
};
struct Options { bool dry_run = false, test = false; };
struct RunResult { bool success; std::string reason; std::uint64_t attempts; };

class IntervalSampler {
    Config config_;
    std::mt19937_64 random_;
public:
    explicit IntervalSampler(const Config& config);
    IntervalSampler(const Config& config, std::uint64_t test_seed);
    Milliseconds next();
};
Milliseconds backoff(const Config&, std::uint64_t streak);
std::optional<WallClock::time_point> retry_after(const std::string&, WallClock::time_point now);
Json clock_report(const platform::ClockHealth&);
RunResult run(const Config&, HttpRequest registration, Runtime&, const Options& = {});
} // namespace itu::polling
