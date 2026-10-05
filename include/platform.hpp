#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace itu::platform {
class ConsoleSession {
public:
    ConsoleSession();
    ~ConsoleSession();
    ConsoleSession(const ConsoleSession&) = delete;
    ConsoleSession& operator=(const ConsoleSession&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class MenuKey { up, down, enter, end, other };
class MenuInput {
public:
    MenuInput();
    ~MenuInput();
    MenuInput(const MenuInput&) = delete;
    MenuInput& operator=(const MenuInput&) = delete;
    MenuKey read();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool read_line(std::string& value, bool password = false, const std::string& prompt = {});
bool is_terminal();
std::optional<std::string> environment(const std::string& name);
// Production entry points only: Windows obtains the original Unicode command line.
std::vector<std::string> arguments(int argc, char** argv);
bool atomic_write_private(const std::filesystem::path& path, const std::string& contents);

// Evidence is independent: enabled network time does not prove synchronization,
// and kernel error bounds are not a measured offset from UTC.
struct ClockHealth {
    std::optional<bool> network_time_enabled, synchronized;
    std::optional<long long> max_error_us, estimated_error_us, age_ms;
    std::string provider, reason;
};
ClockHealth clock_health();

// Process-scoped cancellation, including redirected/nonterminal operation.
class Cancellation {
public:
    Cancellation();
    ~Cancellation();
    Cancellation(const Cancellation&) = delete;
    Cancellation& operator=(const Cancellation&) = delete;
    bool cancelled() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Exclusive, private, crash-persistent client budget storage. Throws on unsafe
// paths, permissions, I/O, concurrent ownership, or contents over 1 MiB.
class PollingStorage {
public:
    explicit PollingStorage(const std::filesystem::path& directory);
    ~PollingStorage();
    PollingStorage(const PollingStorage&) = delete;
    PollingStorage& operator=(const PollingStorage&) = delete;
    std::optional<std::string> read() const;
    void write(const std::string& contents);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class TimingGuard {
public:
    TimingGuard();
    ~TimingGuard();
    TimingGuard(const TimingGuard&) = delete;
    TimingGuard& operator=(const TimingGuard&) = delete;
    void activate();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void cpu_relax();
const char* user_agent();
const char* browser_platform();
} // namespace itu::platform
