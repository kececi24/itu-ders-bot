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
