#include "include/platform.hpp"
#include "test_helpers.hpp"
#include <csignal>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Operation> void rejects(Operation operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, message);
}
void child_lock(const std::filesystem::path& program, const std::filesystem::path& directory) {
#ifdef _WIN32
    std::wstring command = L"\"" + program.native() + L"\" --locked \"" + directory.native() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                         &startup, &process) != FALSE, "start lock contender");
    const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 99);
    DWORD exit = 99;
    GetExitCodeProcess(process.hProcess, &exit);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    check(wait == WAIT_OBJECT_0 && exit == 0, "second process cannot acquire held lock");
#else
    const pid_t child = fork();
    check(child >= 0, "fork lock contender");
    if (child == 0) {
        execl(program.c_str(), program.c_str(), "--locked", directory.c_str(), static_cast<char*>(nullptr));
        _exit(99);
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "second process cannot acquire held lock");
#endif
}
#ifdef _WIN32
void public_read_permission(const std::filesystem::path& path) {
    BYTE sid_buffer[SECURITY_MAX_SID_SIZE];
    DWORD sid_size = sizeof(sid_buffer);
    check(CreateWellKnownSid(WinWorldSid, nullptr, sid_buffer, &sid_size) != FALSE, "create fixture Everyone SID");
    PACL old_acl = nullptr, replacement = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    check(GetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &old_acl, nullptr, &descriptor) == ERROR_SUCCESS, "read fixture ACL");
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_GENERIC_READ;
    access.grfAccessMode = GRANT_ACCESS;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid_buffer);
    const DWORD built = SetEntriesInAclW(1, &access, old_acl, &replacement);
    const DWORD applied = built == ERROR_SUCCESS ? SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, replacement, nullptr) : built;
    if (replacement) LocalFree(replacement);
    LocalFree(descriptor);
    check(applied == ERROR_SUCCESS, "apply public fixture ACL");
}
#endif
}

int main(int argc, char** argv) {
    try {
        const auto args = itu::platform::arguments(argc, argv);
        if (args.size() == 3 && args[1] == "--locked") {
            rejects([&] { itu::platform::PollingStorage contender(std::filesystem::u8path(args[2])); },
                    "contender unexpectedly acquired lock");
            return 0;
        }
        test_helpers::TemporaryDirectory temporary;
        // macOS temp_directory_path commonly has /var as a symlink. Production
        // deliberately rejects symlink ancestors; use an actual directory here.
        const auto root = std::filesystem::canonical(temporary.root);
        const auto directory = root / "runtime";
        const auto program = std::filesystem::canonical(std::filesystem::u8path(args[0]));
        {
            itu::platform::PollingStorage storage(directory);
            check(!storage.read(), "missing state remains absent");
            storage.write("{\"requests\":[1,2],\"deadline\":3}\n");
            check(storage.read() == "{\"requests\":[1,2],\"deadline\":3}\n", "state bytes round trip");
            child_lock(program, directory);
            rejects([&] { itu::platform::PollingStorage duplicate(directory); }, "same-process lock contender rejected");
            storage.write("replacement");
            check(storage.read() == "replacement", "atomic state replacement");
            rejects([&] { storage.write(std::string(1024 * 1024 + 1, 'x')); }, "oversized writes fail closed");
            check(storage.read() == "replacement", "failed write preserves state");
        }
        {
            itu::platform::PollingStorage storage(directory);
            check(storage.read() == "replacement", "state survives lock release/reopen");
            const auto state = directory / "budget.json";
#ifdef _WIN32
            public_read_permission(state);
#else
            check(chmod(state.c_str(), 0644) == 0, "change state permissions");
#endif
            rejects([&] { (void)storage.read(); }, "read refuses nonprivate state");
            rejects([&] { storage.write("unsafe"); }, "write refuses nonprivate state");
            check(itu::platform::atomic_write_private(state, "restored"), "restore private fixture");
            check(storage.read() == "restored", "restored permissions accepted");
            check(itu::platform::atomic_write_private(state, std::string(1024 * 1024 + 1, 'x')), "oversized state fixture");
            rejects([&] { (void)storage.read(); }, "oversized persisted state fails closed");
            check(itu::platform::atomic_write_private(state, "final"), "restore small state fixture");
#ifndef _WIN32
            std::filesystem::remove(state);
            check(symlink((root / "absent").c_str(), state.c_str()) == 0, "create symlink state fixture");
            rejects([&] { (void)storage.read(); }, "symlink state refused");
            rejects([&] { storage.write("unsafe"); }, "symlink state not replaced");
            std::filesystem::remove(state);
            storage.write("hardlink");
            check(link(state.c_str(), (root / "alias").c_str()) == 0, "create hardlink state fixture");
            rejects([&] { (void)storage.read(); }, "hardlinked state refused");
            std::filesystem::remove(root / "alias");
            check(storage.read() == "hardlink", "single-link state restored");
            check(chmod(directory.c_str(), 0755) == 0, "change directory permissions");
            rejects([&] { storage.write("unsafe"); }, "changed private directory fails closed");
            check(chmod(directory.c_str(), 0700) == 0, "restore directory permissions");
#endif
        }
        rejects([&] { itu::platform::PollingStorage traversal(root / "runtime" / ".." / "other"); },
                "parent traversal refused");
#ifndef _WIN32
        check(symlink(directory.c_str(), (root / "alias-dir").c_str()) == 0, "create directory symlink fixture");
        rejects([&] { itu::platform::PollingStorage symlinked(root / "alias-dir"); }, "symlink directory refused");
        check(symlink(root.c_str(), (root / "alias-parent").c_str()) == 0, "create ancestor symlink fixture");
        rejects([&] { itu::platform::PollingStorage symlinked(root / "alias-parent" / "runtime"); }, "symlink ancestor refused");
#endif
        for (const int signal : {SIGINT, SIGTERM}) {
            const auto old = std::signal(signal, SIG_IGN);
            check(old != SIG_ERR, "install fixture signal handler");
            {
                itu::platform::Cancellation cancellation;
                check(!cancellation.cancelled(), "fresh cancellation unset");
                rejects([] { itu::platform::Cancellation second; }, "nested cancellation rejected");
                std::raise(signal);
                check(cancellation.cancelled(), "nonterminal cancellation observed");
            }
            std::raise(signal); // Restored SIG_IGN must prevent process termination.
            std::signal(signal, old);
        }
        const auto health = itu::platform::clock_health();
        check(!health.provider.empty() && !health.reason.empty(), "clock evidence identifies provider and limits");
        check(!health.max_error_us || *health.max_error_us >= 0, "clock max error is nonnegative if available");
        check(!health.estimated_error_us || *health.estimated_error_us >= 0, "clock estimated error is nonnegative if available");
        check(!health.age_ms || *health.age_ms >= 0, "clock evidence age is nonnegative if available");
        std::cout << "Polling platform tests passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
