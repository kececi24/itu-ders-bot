#include "include/platform.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/timex.h>
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
            errno = 0;
            acl_t acl = acl_get_fd(fd);
            if (acl != nullptr) {
                written = acl_valid(acl) == 0;
                if (written) {
                    acl_entry_t entry;
                    errno = 0;
                    const int result = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
                    // Darwin reports an exhausted valid ACL as -1/EINVAL.
                    // Any entry or unexpected error must fail before writing.
                    written = result == -1 && errno == EINVAL;
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

ClockHealth clock_health() {
    ClockHealth result;
#ifdef __APPLE__
    result.provider = "kernel-ntp_gettime";
    ntptimeval value{};
    const int state = ntp_gettime(&value);
#else
    result.provider = "kernel-adjtimex";
    timex value{}; // modes == 0 is a read-only query, never a clock adjustment.
    const int state = adjtimex(&value);
#endif
    if (state < 0) {
        result.reason = errno == EACCES || errno == EPERM ? "kernel-query-permission-denied" : "kernel-query-unavailable";
        return result;
    }
    if (state >= TIME_OK && state <= TIME_WAIT) result.synchronized = true;
    else if (state == TIME_ERROR) result.synchronized = false;
    if (value.maxerror >= 0) result.max_error_us = value.maxerror;
    if (value.esterror >= 0) result.estimated_error_us = value.esterror;
    result.reason = "kernel-state-only; network-time-enablement-and-UTC-offset-unverified";
#ifndef __APPLE__
    // This root-owned marker is evidence of a past timesyncd synchronization,
    // not proof that its service is currently enabled or still accurate.
    struct stat marker{};
    if (lstat("/run/systemd/timesync/synchronized", &marker) == 0 &&
        S_ISREG(marker.st_mode) && marker.st_uid == 0 && !(marker.st_mode & 0022)) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now).count() -
                         static_cast<long long>(marker.st_mtim.tv_sec) * 1000 - marker.st_mtim.tv_nsec / 1000000;
        if (age >= 0) result.age_ms = age;
        result.provider += "; systemd-timesyncd-marker";
    }
#endif
    return result;
}

namespace {
std::atomic<bool> cancellation_active{false};
volatile std::sig_atomic_t cancellation_signal = 0;
void cancel_signal(int signal) { cancellation_signal = signal; }
struct Descriptor {
    int value = -1;
    explicit Descriptor(int fd = -1) : value(fd) {}
    ~Descriptor() { if (value >= 0) close(value); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int release() { const int fd = value; value = -1; return fd; }
};
constexpr size_t polling_state_limit = 1024 * 1024;
bool empty_acl(int fd) {
#ifdef __APPLE__
    errno = 0;
    acl_t acl = acl_get_fd(fd);
    if (!acl) return errno == ENOENT;
    acl_entry_t entry;
    const bool valid = acl_valid(acl) == 0;
    errno = 0;
    const int next = valid ? acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) : 0;
    const bool empty = valid && next == -1 && errno == EINVAL;
    acl_free(acl);
    return empty;
#else
    // A POSIX ACL granting other users access requires nonzero group-class mode
    // bits; the exact owner-only mode check below rejects that case.
    (void)fd;
    return true;
#endif
}
void clear_new_acl(int fd) {
#ifdef __APPLE__
    acl_t acl = acl_init(0);
    if (!acl) throw std::runtime_error("Unable to initialize private polling permissions");
    const int result = acl_set_fd(fd, acl);
    acl_free(acl);
    if (result != 0) throw std::runtime_error("Unable to set private polling permissions");
#else
    (void)fd;
#endif
}
void private_descriptor(int fd, bool directory) {
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_uid != geteuid() ||
        (st.st_mode & 07777) != (directory ? 0700 : 0600) ||
        (directory ? !S_ISDIR(st.st_mode) : (!S_ISREG(st.st_mode) || st.st_nlink != 1)) || !empty_acl(fd))
        throw std::runtime_error("Polling storage requires private owner-only files and directory");
}
int open_polling_directory(const std::filesystem::path& path, bool create) {
    if (path.empty() || path.filename().empty() || path.filename() == "." || path.filename() == "..")
        throw std::runtime_error("Invalid polling storage directory");
    Descriptor parent(open(path.is_absolute() ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (parent.value < 0) throw std::runtime_error("Unable to open polling storage parent");
    const auto relative = path.relative_path();
    for (auto part = relative.begin(); part != relative.end(); ++part) {
        if (*part == "..") throw std::runtime_error("Polling storage cannot traverse parent directories");
        if (*part == "." || part->empty()) continue;
        auto next = part;
        const bool last = ++next == relative.end();
        bool created = false;
        if (last && create) {
            if (mkdirat(parent.value, part->c_str(), 0700) == 0) created = true;
            else if (errno != EEXIST) throw std::runtime_error("Unable to create polling storage directory");
        }
        Descriptor child(openat(parent.value, part->c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (child.value < 0) throw std::runtime_error("Polling storage directory is unavailable or a symlink");
        if (created) clear_new_acl(child.value);
        if (last) private_descriptor(child.value, true);
        close(parent.value);
        parent.value = child.release();
    }
    return parent.release();
}
}

struct Cancellation::Impl {
    struct sigaction old_int{}, old_term{};
    bool int_set = false, term_set = false;
    void restore() noexcept {
        if (term_set) sigaction(SIGTERM, &old_term, nullptr);
        if (int_set) sigaction(SIGINT, &old_int, nullptr);
        cancellation_active.store(false);
    }
    Impl() {
        if (cancellation_active.exchange(true)) throw std::runtime_error("Cancellation handler already active");
        cancellation_signal = 0;
        struct sigaction action{};
        action.sa_handler = cancel_signal;
        sigemptyset(&action.sa_mask);
        try {
            if (sigaction(SIGINT, &action, &old_int) != 0) throw std::runtime_error("Unable to install polling cancellation");
            int_set = true;
            if (sigaction(SIGTERM, &action, &old_term) != 0) throw std::runtime_error("Unable to install polling cancellation");
            term_set = true;
        } catch (...) { restore(); throw; }
    }
    ~Impl() { restore(); }
};
Cancellation::Cancellation() : impl_(std::make_unique<Impl>()) {}
Cancellation::~Cancellation() = default;
bool Cancellation::cancelled() const { return cancellation_signal != 0; }

struct PollingStorage::Impl {
    std::filesystem::path path;
    Descriptor directory, lock;
    explicit Impl(const std::filesystem::path& value) : path(value), directory(open_polling_directory(value, true)) {
        bool created = false;
        int fd = openat(directory.value, "lock", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) created = true;
        else if (errno == EEXIST) fd = openat(directory.value, "lock", O_RDWR | O_NOFOLLOW | O_CLOEXEC);
        lock.value = fd;
        if (fd < 0) throw std::runtime_error("Unable to open polling instance lock");
        if (created) clear_new_acl(fd);
        private_descriptor(fd, false);
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) throw std::runtime_error("Polling storage is already locked or locking is unavailable");
        validate();
    }
    void validate() const {
        private_descriptor(directory.value, true);
        private_descriptor(lock.value, false);
        Descriptor current(open_polling_directory(path, false));
        struct stat saved{}, actual{}, lock_path{}, lock_fd{};
        if (fstat(directory.value, &saved) != 0 || fstat(current.value, &actual) != 0 ||
            saved.st_dev != actual.st_dev || saved.st_ino != actual.st_ino ||
            fstatat(directory.value, "lock", &lock_path, AT_SYMLINK_NOFOLLOW) != 0 ||
            fstat(lock.value, &lock_fd) != 0 || lock_path.st_dev != lock_fd.st_dev || lock_path.st_ino != lock_fd.st_ino)
            throw std::runtime_error("Polling storage location changed while locked");
    }
    std::optional<std::string> read() const {
        validate();
        Descriptor state(openat(directory.value, "budget.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        if (state.value < 0) {
            if (errno == ENOENT) return std::nullopt;
            throw std::runtime_error("Unable to read private polling state");
        }
        private_descriptor(state.value, false);
        std::string contents;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::read(state.value, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) throw std::runtime_error("Unable to read polling state");
            if (count == 0) break;
            if (contents.size() + static_cast<size_t>(count) > polling_state_limit)
                throw std::runtime_error("Polling state exceeds size limit");
            contents.append(buffer, static_cast<size_t>(count));
        }
        return contents;
    }
    void write(const std::string& contents) {
        if (contents.size() > polling_state_limit) throw std::runtime_error("Polling state exceeds size limit");
        (void)read(); // Validate existing state before replacing it; unsafe state fails closed.
        static std::atomic<unsigned long long> sequence{0};
        Descriptor temporary;
        std::string name;
        for (int attempt = 0; attempt < 64; ++attempt) {
            name = ".budget.tmp." + std::to_string(getpid()) + "." + std::to_string(sequence.fetch_add(1));
            temporary.value = openat(directory.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (temporary.value >= 0) break;
            if (errno != EEXIST) throw std::runtime_error("Unable to create polling state replacement");
        }
        if (temporary.value < 0) throw std::runtime_error("Unable to create polling state replacement");
        try {
            clear_new_acl(temporary.value);
            private_descriptor(temporary.value, false);
            size_t offset = 0;
            while (offset < contents.size()) {
                const ssize_t count = ::write(temporary.value, contents.data() + offset, contents.size() - offset);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) throw std::runtime_error("Unable to write polling state");
                offset += static_cast<size_t>(count);
            }
            int synced;
            do { synced = fsync(temporary.value); } while (synced < 0 && errno == EINTR);
            if (synced != 0) throw std::runtime_error("Unable to persist polling state");
            validate();
            if (renameat(directory.value, name.c_str(), directory.value, "budget.json") != 0)
                throw std::runtime_error("Unable to replace polling state");
            do { synced = fsync(directory.value); } while (synced < 0 && errno == EINTR);
            if (synced != 0) throw std::runtime_error("Unable to persist polling state directory");
        } catch (...) { unlinkat(directory.value, name.c_str(), 0); throw; }
    }
};
PollingStorage::PollingStorage(const std::filesystem::path& directory) : impl_(std::make_unique<Impl>(directory)) {}
PollingStorage::~PollingStorage() = default;
std::optional<std::string> PollingStorage::read() const { return impl_->read(); }
void PollingStorage::write(const std::string& contents) { impl_->write(contents); }

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
