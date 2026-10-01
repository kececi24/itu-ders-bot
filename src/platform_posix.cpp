#include "include/platform.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/acl.h>
#endif
#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace itu::platform {
namespace {
volatile std::sig_atomic_t interrupted = 0;
void on_signal(int signal) { interrupted = signal; }

// Only one interactive reader is active at a time. Handlers only set a flag;
// restoration happens in normal execution, including when input is interrupted.
class Input {
    termios saved_{};
    int flags_ = -1;
    struct sigaction old_int_{}, old_term_{};
    bool int_set_ = false, term_set_ = false, mode_set_ = false;

    void restore() noexcept {
        if (mode_set_) {
            while (tcsetattr(STDIN_FILENO, TCSANOW, &saved_) < 0 && errno == EINTR) {}
        }
        if (flags_ >= 0) fcntl(STDIN_FILENO, F_SETFL, flags_);
        if (term_set_) sigaction(SIGTERM, &old_term_, nullptr);
        if (int_set_) sigaction(SIGINT, &old_int_, nullptr);
    }
public:
    Input(bool menu, bool password) {
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
            throw std::runtime_error("Interactive input requires a terminal");
        if (tcgetattr(STDIN_FILENO, &saved_) < 0)
            throw std::runtime_error("Unable to read terminal settings");
        flags_ = fcntl(STDIN_FILENO, F_GETFL);
        if (flags_ < 0) throw std::runtime_error("Unable to read input flags");
        try {
            interrupted = 0;
            struct sigaction action{};
            action.sa_handler = on_signal;
            sigemptyset(&action.sa_mask);
            if (sigaction(SIGINT, &action, &old_int_) < 0)
                throw std::runtime_error("Unable to handle terminal interruption");
            int_set_ = true;
            if (sigaction(SIGTERM, &action, &old_term_) < 0)
                throw std::runtime_error("Unable to handle terminal interruption");
            term_set_ = true;
            auto mode = saved_;
            if (menu) {
                mode.c_lflag &= ~(ICANON | ECHO | ECHONL);
                mode.c_cc[VMIN] = 1;
                mode.c_cc[VTIME] = 0;
            }
            if (password) mode.c_lflag &= ~(ECHO | ECHONL);
            if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) < 0)
                throw std::runtime_error("Unable to change terminal settings");
            mode_set_ = true;
            // Nonblocking reads close the signal-arrival race between poll/read.
            if (fcntl(STDIN_FILENO, F_SETFL, flags_ | O_NONBLOCK) < 0)
                throw std::runtime_error("Unable to enable interruptible input");
        } catch (...) { restore(); throw; }
    }
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    ~Input() { restore(); }

    int get(int timeout_ms = -1) {
        int elapsed = 0;
        for (;;) {
            if (interrupted) throw std::runtime_error("Terminal input interrupted");
            int interval = timeout_ms < 0 ? 100 : std::min(100, timeout_ms - elapsed);
            pollfd descriptor{STDIN_FILENO, POLLIN, 0};
            int ready = poll(&descriptor, 1, interval);
            if (ready < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("Unable to poll terminal input");
            }
            if (interrupted) throw std::runtime_error("Terminal input interrupted");
            if (ready == 0) {
                elapsed += interval;
                if (timeout_ms >= 0 && elapsed >= timeout_ms) return -2;
                continue;
            }
            char value;
            ssize_t count = read(STDIN_FILENO, &value, 1);
            if (count == 1) return static_cast<unsigned char>(value);
            if (count == 0) return -1;
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                throw std::runtime_error("Unable to read terminal input");
        }
    }
};

} // namespace

struct ConsoleSession::Impl {};
ConsoleSession::ConsoleSession() : impl_(std::make_unique<Impl>()) {}
ConsoleSession::~ConsoleSession() = default;
struct MenuInput::Impl { Input input{true, false}; };
MenuInput::MenuInput() : impl_(std::make_unique<Impl>()) {}
MenuInput::~MenuInput() = default;
MenuKey MenuInput::read() {
    const int key = impl_->input.get();
    if (key < 0 || key == 4) return MenuKey::end;
    if (key == '\r' || key == '\n') return MenuKey::enter;
    if (key == 27) {
        const int prefix = impl_->input.get(100);
        if (prefix == '[' || prefix == 'O') {
            const int direction = impl_->input.get(100);
            if (direction == 'A') return MenuKey::up;
            if (direction == 'B') return MenuKey::down;
        }
    }
    return MenuKey::other;
}

bool read_line(std::string& value, bool password, const std::string& prompt) {
    Input input(false, password);
    value.clear();
    std::cout << prompt << std::flush;
    for (;;) {
        const int key = input.get();
        if (key < 0) {
            if (!value.empty() && value.back() == '\r') value.pop_back();
            return !value.empty();
        }
        if (key == '\n') {
            if (!value.empty() && value.back() == '\r') value.pop_back();
            return true;
        }
        value.push_back(static_cast<char>(key));
    }
}
bool is_terminal() { return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0; }
std::optional<std::string> environment(const std::string& name) {
    if (const char* value = std::getenv(name.c_str())) return std::string(value);
    return std::nullopt;
}
std::vector<std::string> arguments(int argc, char** argv) { return {argv, argv + argc}; }

bool atomic_write_private(const std::filesystem::path& path, const std::string& contents) {
    std::string pattern = path.native() + ".tmp.XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    int fd = mkstemp(temporary.data());
    if (fd < 0) return false;
    struct stat permissions{};
    // Some mounted filesystems accept chmod but do not enforce its mode.
    // Refuse the replacement before writing credentials in that case.
    bool written = fchmod(fd, S_IRUSR | S_IWUSR) == 0 && fstat(fd, &permissions) == 0 &&
                   (permissions.st_mode & 0777) == (S_IRUSR | S_IWUSR) && permissions.st_uid == geteuid();
#ifdef __APPLE__
    if (written) {
        acl_t empty_acl = acl_init(0);
        if (empty_acl) {
            if (acl_set_fd(fd, empty_acl) != 0) {
                written = false;
            }
            acl_free(empty_acl);
        } else {
            written = false;
        }
        if (written) {
            acl_t acl = acl_get_fd(fd);
            if (acl != nullptr) {
                acl_entry_t entry;
                if (acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) == 0) {
                    written = false;
                }
                acl_free(acl);
            } else if (errno != ENOENT) {
                written = false;
            }
        }
    }
#endif
    size_t offset = 0;
    while (written && offset < contents.size()) {
        const ssize_t count = write(fd, contents.data() + offset, contents.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { written = false; break; }
        offset += static_cast<size_t>(count);
    }
    if (written) {
        int sync_result;
        do { sync_result = fsync(fd); } while (sync_result < 0 && errno == EINTR);
        if (sync_result < 0) written = false;
    }
    if (close(fd) < 0) written = false;
    if (!written || rename(temporary.data(), path.c_str()) < 0) {
        unlink(temporary.data());
        return false;
    }
    return true;
}

struct TimingGuard::Impl {
#ifdef __APPLE__
    bool active = false;
    qos_class_t previous = QOS_CLASS_UNSPECIFIED;
    int relative_priority = 0;
    ~Impl() {
        if (active) pthread_set_qos_class_self_np(previous, relative_priority);
    }
    void activate() {
        if (active) return;
        const qos_class_t current = qos_class_self();
        if (current >= QOS_CLASS_USER_INITIATED) return;
        if (pthread_get_qos_class_np(pthread_self(), &previous, &relative_priority) != 0) return;
        active = pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0) == 0;
    }
#else
    void activate() {} // Linux: no privileged scheduling changes.
#endif
};
TimingGuard::TimingGuard() : impl_(std::make_unique<Impl>()) {}
TimingGuard::~TimingGuard() = default;
void TimingGuard::activate() { impl_->activate(); }
void cpu_relax() {
#if defined(__aarch64__)
    __asm__ __volatile__("yield");
#elif defined(__x86_64__)
    _mm_pause();
#else
#error Unsupported CPU architecture
#endif
}
const char* user_agent() {
#ifdef __APPLE__
    return "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36";
#else
    return "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36";
#endif
}
const char* browser_platform() {
#ifdef __APPLE__
    return "\"macOS\"";
#else
    return "\"Linux\"";
#endif
}
} // namespace itu::platform
