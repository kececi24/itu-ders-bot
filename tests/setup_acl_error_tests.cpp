// Compile the real adapter with test-local syscall wrappers. No seams enter production.
#include "include/platform.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <stdexcept>
#include <sys/acl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

namespace {
enum class Fault { none, empty, read_error, read_without_errno, invalid_acl,
                   entry_error, entry_without_errno, entry_unexpected, entry_present,
                   free_errno, set_error, init_error };
Fault fault = Fault::none;
int injected_errno = EIO;
int writes = 0;
acl_t read_acl(int fd, const char* file, acl_type_t type) {
    if (fault == Fault::read_error) { errno = injected_errno; return nullptr; }
    if (fault == Fault::read_without_errno) return nullptr;
    if (fault == Fault::empty || fault == Fault::invalid_acl || fault == Fault::entry_error ||
        fault == Fault::entry_without_errno || fault == Fault::entry_unexpected ||
        fault == Fault::entry_present || fault == Fault::free_errno) return ::acl_init(0);
    return file ? ::acl_get_file(file, type) : ::acl_get_fd(fd);
}
acl_t checked_get_fd(int fd) { return read_acl(fd, nullptr, ACL_TYPE_EXTENDED); }
acl_t checked_get_file(const char* path, acl_type_t type) { return read_acl(-1, path, type); }
int checked_valid(acl_t acl) {
    if (fault == Fault::invalid_acl) { errno = EINVAL; return -1; }
    return ::acl_valid(acl);
}
int checked_entry(acl_t acl, int index, acl_entry_t* entry) {
    if (fault == Fault::entry_error) { errno = injected_errno; return -1; }
    if (fault == Fault::entry_without_errno) return -1;
    if (fault == Fault::entry_unexpected) { errno = EINVAL; return 1; }
    if (fault == Fault::entry_present) return 0;
    return ::acl_get_entry(acl, index, entry);
}
int checked_free(void* acl) {
    const int result = ::acl_free(acl);
    if (fault == Fault::free_errno) errno = EIO;
    return result;
}
int checked_set_fd(int fd, acl_t acl) {
    if (fault == Fault::set_error) { errno = EOPNOTSUPP; return -1; }
    return ::acl_set_fd(fd, acl);
}
acl_t checked_init(int count) {
    if (fault == Fault::init_error) { errno = ENOMEM; return nullptr; }
    return ::acl_init(count);
}
ssize_t checked_write(int fd, const void* data, size_t size) {
    ++writes;
    return ::write(fd, data, size);
}
}

#define acl_get_fd checked_get_fd
#define acl_get_file checked_get_file
#define acl_get_entry checked_entry
#define acl_valid checked_valid
#define acl_free checked_free
#define acl_set_fd checked_set_fd
#define acl_init checked_init
#define write checked_write
#include "src/platform_posix.cpp"
#include "tests/test_helpers.hpp"
#undef write
#undef acl_init
#undef acl_set_fd
#undef acl_free
#undef acl_valid
#undef acl_get_entry
#undef acl_get_file
#undef acl_get_fd

namespace {
std::string contents(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void run_case(Fault mode, bool accepted, bool helper_rejects) {
    test_helpers::TemporaryDirectory directory;
    const auto existing = directory.root / "existing.env";
    const auto fresh = directory.root / "new.env";
    fault = Fault::none;
    test_helpers::require(itu::platform::atomic_write_private(existing, "old fixture"), "control creation");
    for (const auto& path : {existing, fresh}) {
        writes = 0;
        fault = mode;
        // An API failure that leaves errno untouched must not inherit a benign error.
        errno = (mode == Fault::read_without_errno) ? ENOENT : EINVAL;
        const bool result = itu::platform::atomic_write_private(path, "new fixture");
        test_helpers::require(result == accepted, "writer accepted an unexpected ACL result");
        if (!accepted) {
            test_helpers::require(writes == 0, "ACL failure must occur before any credential write");
            test_helpers::require(contents(existing) == "old fixture", "preserve original on ACL failure");
            test_helpers::require(!std::filesystem::exists(fresh), "failed create must leave no destination");
        } else {
            test_helpers::require(writes > 0 && contents(path) == "new fixture", "legitimate ACL control");
        }
        for (const auto& child : std::filesystem::directory_iterator(directory.root))
            test_helpers::require(child.path().filename().string().find(".tmp.") == std::string::npos,
                                  "failed ACL verification leaked temporary file");
    }
    fault = mode;
    errno = (mode == Fault::read_without_errno) ? ENOENT : EINVAL;
    bool rejected = false;
    try { test_helpers::private_file(existing); }
    catch (const std::runtime_error&) { rejected = true; }
    test_helpers::require(rejected == helper_rejects, "private-file helper accepted an unexpected ACL result");
    fault = Fault::none;
}
}

int main() {
    try {
        run_case(Fault::none, true, false);
        run_case(Fault::empty, true, false);
        run_case(Fault::free_errno, true, false);
        for (int error : {EIO, EACCES, ENOMEM, EOPNOTSUPP, EINVAL}) {
            injected_errno = error;
            run_case(Fault::read_error, false, true);
        }
        for (int error : {EIO, EPERM, ENOMEM}) {
            injected_errno = error;
            run_case(Fault::entry_error, false, true);
        }
        run_case(Fault::invalid_acl, false, true);
        run_case(Fault::read_without_errno, false, true);
        run_case(Fault::entry_without_errno, false, true);
        run_case(Fault::entry_unexpected, false, true);
        run_case(Fault::entry_present, false, true);
        run_case(Fault::set_error, false, false);
        run_case(Fault::init_error, false, false);
        std::cout << "ACL failures rejected before writes; create/replace controls and cleanup passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
