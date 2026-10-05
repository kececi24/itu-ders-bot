#include "include/platform.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <immintrin.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <csignal>
#include <iostream>
#include <stdexcept>

namespace itu::platform {
namespace {
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() > INT_MAX) throw std::runtime_error("UTF-8 value is too large");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 input");
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size))
        throw std::runtime_error("Unable to convert UTF-8 input");
    return result;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > INT_MAX) throw std::runtime_error("Unicode value is too large");
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid Unicode input");
    std::string result(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
        throw std::runtime_error("Unable to convert Unicode input");
    return result;
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    bool close() {
        HANDLE handle = value;
        value = INVALID_HANDLE_VALUE;
        return CloseHandle(handle) != FALSE;
    }
};

std::atomic<bool> interrupted{false};
// Windows dispatches control handlers on another thread. Coordinate reader
// lifetime so a late cancellation cannot inject a wakeup into the caller's shell.
SRWLOCK reader_lock = SRWLOCK_INIT;
bool reader_active = false;
bool cooked_reader = false;
HANDLE control_input = INVALID_HANDLE_VALUE;
BOOL WINAPI on_control(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    BOOL handled = FALSE;
    AcquireSRWLockShared(&reader_lock);
    if (reader_active) {
        interrupted.store(true);
        // Wake cooked ReadConsoleW while retaining native line editing. The
        // result is discarded and terminal restoration runs outside the handler.
        if (cooked_reader) {
            INPUT_RECORD wake{};
            wake.EventType = KEY_EVENT;
            wake.Event.KeyEvent.bKeyDown = TRUE;
            wake.Event.KeyEvent.wRepeatCount = 1;
            wake.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
            wake.Event.KeyEvent.uChar.UnicodeChar = L'\r';
            DWORD written = 0;
            WriteConsoleInputW(control_input, &wake, 1, &written);
        }
        handled = TRUE;
    }
    ReleaseSRWLockShared(&reader_lock);
    return handled;
}
void check_interrupted() {
    if (interrupted.load()) throw std::runtime_error("Terminal input interrupted");
}
class InputMode {
    Handle wake_input_;
    DWORD saved_ = 0;
    bool handler_ = false, changed_ = false;
public:
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    InputMode(bool menu, bool password) {
        DWORD output_mode = 0;
        if (!GetConsoleMode(input, &saved_) ||
            !GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode))
            throw std::runtime_error("Interactive input requires a terminal");
        if (!menu) {
            wake_input_.value = CreateFileW(L"CONIN$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr, OPEN_EXISTING, 0, nullptr);
            if (wake_input_.value == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Unable to enable interruptible terminal input");
        }
        AcquireSRWLockExclusive(&reader_lock);
        interrupted.store(false);
        control_input = menu ? input : wake_input_.value;
        cooked_reader = !menu;
        reader_active = true;
        ReleaseSRWLockExclusive(&reader_lock);
        if (!SetConsoleCtrlHandler(on_control, TRUE)) {
            AcquireSRWLockExclusive(&reader_lock);
            reader_active = false;
            ReleaseSRWLockExclusive(&reader_lock);
            throw std::runtime_error("Unable to handle terminal interruption");
        }
        handler_ = true;
        DWORD mode = saved_ | ENABLE_PROCESSED_INPUT;
        if (menu) {
            mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_QUICK_EDIT_MODE);
            mode |= ENABLE_EXTENDED_FLAGS;
        } else {
            mode |= ENABLE_LINE_INPUT;
            mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
            if (password) mode &= ~ENABLE_ECHO_INPUT;
        }
        if (!SetConsoleMode(input, mode)) {
            AcquireSRWLockExclusive(&reader_lock);
            reader_active = false;
            ReleaseSRWLockExclusive(&reader_lock);
            SetConsoleCtrlHandler(on_control, FALSE);
            handler_ = false;
            throw std::runtime_error("Unable to change terminal settings");
        }
        changed_ = true;
    }
    InputMode(const InputMode&) = delete;
    InputMode& operator=(const InputMode&) = delete;
    ~InputMode() {
        AcquireSRWLockExclusive(&reader_lock);
        reader_active = false;
        if (changed_) SetConsoleMode(input, saved_);
        if (interrupted.load()) FlushConsoleInputBuffer(input);
        ReleaseSRWLockExclusive(&reader_lock);
        if (handler_) SetConsoleCtrlHandler(on_control, FALSE);
    }
};

struct PrivateSecurity {
    std::vector<unsigned char> token_user;
    PACL acl = nullptr;
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
    PrivateSecurity(const PrivateSecurity&) = delete;
    PrivateSecurity& operator=(const PrivateSecurity&) = delete;
    PrivateSecurity() = default;
    ~PrivateSecurity() { if (acl) LocalFree(acl); }
    bool initialize() {
        Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return false;
        DWORD size = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
        if (!size) return false;
        token_user.resize(size);
        if (!GetTokenInformation(token.value, TokenUser, token_user.data(), size, &size)) return false;
        PSID owner = reinterpret_cast<TOKEN_USER*>(token_user.data())->User.Sid;
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_ALL_ACCESS;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = static_cast<LPWSTR>(owner);
        return SetEntriesInAclW(1, &access, nullptr, &acl) == ERROR_SUCCESS &&
               InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
               SetSecurityDescriptorOwner(&descriptor, owner, FALSE) &&
               SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) &&
               SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    }
};
bool supports_private_permissions(const std::filesystem::path& path) {
    std::error_code error;
    const auto parent = std::filesystem::absolute(path, error).parent_path();
    if (error) return false;
    std::vector<wchar_t> root(32768);
    if (!GetVolumePathNameW(parent.c_str(), root.data(), static_cast<DWORD>(root.size()))) return false;
    DWORD flags = 0;
    return GetVolumeInformationW(root.data(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) &&
           (flags & FILE_PERSISTENT_ACLS);
}
} // namespace

struct ConsoleSession::Impl {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    DWORD output_mode = 0, error_mode = 0;
    UINT input_cp = 0, output_cp = 0;
    bool output_changed = false, error_changed = false, input_cp_changed = false, output_cp_changed = false;
    void restore() noexcept {
        // Distinct standard handles may refer to the same screen buffer.
        // Restore in reverse acquisition order so its original mode wins.
        if (error_changed) SetConsoleMode(error, error_mode);
        if (output_changed) SetConsoleMode(output, output_mode);
        if (input_cp_changed) SetConsoleCP(input_cp);
        if (output_cp_changed) SetConsoleOutputCP(output_cp);
    }
    Impl() {
        try {
            // Redirected streams remain ordinary UTF-8 bytes, not console API I/O.
            if (GetConsoleMode(output, &output_mode)) {
                if (!SetConsoleMode(output, output_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
                    throw std::runtime_error("Unable to enable terminal output");
                output_changed = true;
            }
            if (error != output && GetConsoleMode(error, &error_mode)) {
                if (!SetConsoleMode(error, error_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
                    throw std::runtime_error("Unable to enable terminal error output");
                error_changed = true;
            }
            input_cp = GetConsoleCP();
            output_cp = GetConsoleOutputCP();
            if (input_cp) {
                if (!SetConsoleCP(CP_UTF8)) throw std::runtime_error("Unable to enable Unicode terminal input");
                input_cp_changed = true;
            }
            if (output_cp) {
                if (!SetConsoleOutputCP(CP_UTF8)) throw std::runtime_error("Unable to enable Unicode terminal output");
                output_cp_changed = true;
            }
        } catch (...) { restore(); throw; }
    }
    ~Impl() { restore(); }
};
ConsoleSession::ConsoleSession() : impl_(std::make_unique<Impl>()) {}
ConsoleSession::~ConsoleSession() = default;
struct MenuInput::Impl {
    InputMode mode{true, false};
    MenuKey pending_key = MenuKey::other;
    WORD repeat_remaining = 0;
};
MenuInput::MenuInput() : impl_(std::make_unique<Impl>()) {}
MenuInput::~MenuInput() = default;
MenuKey MenuInput::read() {
    if (impl_->repeat_remaining > 0) {
        --impl_->repeat_remaining;
        return impl_->pending_key;
    }
    for (;;) {
        check_interrupted();
        const DWORD ready = WaitForSingleObject(impl_->mode.input, 100);
        check_interrupted();
        if (ready == WAIT_TIMEOUT) continue;
        if (ready != WAIT_OBJECT_0) throw std::runtime_error("Unable to wait for terminal input");
        INPUT_RECORD record{};
        DWORD count = 0;
        if (!ReadConsoleInputW(impl_->mode.input, &record, 1, &count))
            throw std::runtime_error("Unable to read terminal input");
        check_interrupted();
        if (!count || record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
        const auto& key = record.Event.KeyEvent;
        MenuKey key_type = MenuKey::other;
        if (key.wVirtualKeyCode == VK_UP) key_type = MenuKey::up;
        else if (key.wVirtualKeyCode == VK_DOWN) key_type = MenuKey::down;
        else if (key.wVirtualKeyCode == VK_RETURN) key_type = MenuKey::enter;
        else if (key.uChar.UnicodeChar == 4 || key.uChar.UnicodeChar == 26) key_type = MenuKey::end;
        else if (key.uChar.UnicodeChar == 3) throw std::runtime_error("Terminal input interrupted");

        if ((key_type == MenuKey::up || key_type == MenuKey::down) && key.wRepeatCount > 1) {
            impl_->pending_key = key_type;
            impl_->repeat_remaining = key.wRepeatCount - 1;
        }
        return key_type;
    }
}
bool read_line(std::string& value, bool password, const std::string& prompt) {
    InputMode mode(false, password);
    value.clear();
    std::cout << prompt << std::flush;
    std::wstring line;
    for (;;) {
        check_interrupted();
        wchar_t buffer[256];
        DWORD count = 0;
        const BOOL success = ReadConsoleW(mode.input, buffer, 256, &count, nullptr);
        const DWORD error = success ? ERROR_SUCCESS : GetLastError();
        check_interrupted();
        if (!success) {
            if (error == ERROR_OPERATION_ABORTED) throw std::runtime_error("Terminal input interrupted");
            throw std::runtime_error("Unable to read terminal input");
        }
        if (!count) { value = utf8(line); return !value.empty(); }
        line.append(buffer, count);
        if (line.find(L'\n') != std::wstring::npos) {
            if (!line.empty() && line.front() == 26) return false;
            const auto end = line.find(L'\n');
            line.resize(end);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            value = utf8(line);
            return true;
        }
    }
}
bool is_terminal() {
    DWORD in_mode = 0, out_mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &in_mode) != FALSE &&
           GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &out_mode) != FALSE;
}
std::optional<std::string> environment(const std::string& name) {
    const auto key = wide(name);
    SetLastError(ERROR_SUCCESS);
    DWORD required = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (!required) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
        if (GetLastError() == ERROR_SUCCESS) return std::string{};
        throw std::runtime_error("Unable to read environment variable");
    }
    // Another thread may update the environment between the sizing and read calls.
    for (;;) {
        std::wstring value(required, L'\0');
        SetLastError(ERROR_SUCCESS);
        const DWORD copied = GetEnvironmentVariableW(key.c_str(), value.data(), required);
        if (copied >= required) { required = copied; continue; }
        if (!copied && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
        if (!copied && GetLastError() != ERROR_SUCCESS) throw std::runtime_error("Unable to read environment variable");
        value.resize(copied);
        return utf8(value);
    }
}
std::vector<std::string> arguments(int, char**) {
    int count = 0;
    LPWSTR* native = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!native) throw std::runtime_error("Unable to read command line");
    std::vector<std::string> result;
    try {
        for (int index = 0; index < count; ++index) result.push_back(utf8(native[index]));
    } catch (...) { LocalFree(native); throw; }
    LocalFree(native);
    return result;
}
bool atomic_write_private(const std::filesystem::path& path, const std::string& contents) {
    if (!supports_private_permissions(path)) return false;
    PrivateSecurity security;
    if (!security.initialize()) return false;
    static std::atomic<unsigned long long> counter{0};
    std::filesystem::path temporary;
    Handle file;
    for (int attempt = 0; attempt < 64; ++attempt) {
        temporary = path;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                     std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(counter.fetch_add(1));
        file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, &security.attributes,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    if (file.value == INVALID_HANDLE_VALUE) return false;
    bool written = true;
    size_t offset = 0;
    while (offset < contents.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(contents.size() - offset, MAXDWORD));
        DWORD count = 0;
        if (!WriteFile(file.value, contents.data() + offset, chunk, &count, nullptr) || !count) {
            written = false;
            break;
        }
        offset += count;
    }
    if (!FlushFileBuffers(file.value)) written = false;
    if (!file.close()) written = false;
    // Both paths are in one directory. Never allow copy-across-volume fallback:
    // the private temporary file's descriptor must accompany the replacement.
    if (!written || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

ClockHealth clock_health() {
    ClockHealth result;
    result.provider = "W32Time";
    // Native local registry/service queries have bounded buffers and never
    // launch w32tm, parse localized text, contact a peer, or change settings.
    wchar_t type[64]{};
    DWORD type_size = sizeof(type), enabled = 0, enabled_size = sizeof(enabled);
    const auto type_status = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\W32Time\\Parameters", L"Type",
        RRF_RT_REG_SZ, nullptr, type, &type_size);
    auto enabled_status = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\W32Time\\TimeProviders\\NtpClient", L"Enabled",
        RRF_RT_REG_DWORD, nullptr, &enabled, &enabled_size);
    // A configured policy overrides the service's local configuration. Do not
    // treat inaccessible or malformed policy data as absence of a policy.
    if (enabled_status == ERROR_FILE_NOT_FOUND || enabled_status == ERROR_PATH_NOT_FOUND) {
        enabled_size = sizeof(enabled);
        enabled_status = RegGetValueW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services\\W32Time\\TimeProviders\\NtpClient", L"Enabled",
            RRF_RT_REG_DWORD, nullptr, &enabled, &enabled_size);
    }
    if (type_status == ERROR_SUCCESS && enabled_status == ERROR_SUCCESS) {
        if (_wcsicmp(type, L"NoSync") == 0 || enabled == 0) result.network_time_enabled = false;
        else if (enabled == 1 && (_wcsicmp(type, L"NTP") == 0 || _wcsicmp(type, L"NT5DS") == 0 ||
                                  _wcsicmp(type, L"AllSync") == 0)) result.network_time_enabled = true;
    }
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        result.reason = GetLastError() == ERROR_ACCESS_DENIED ? "W32Time-service-query-permission-denied" : "W32Time-service-query-unavailable";
        return result;
    }
    SC_HANDLE service = OpenServiceW(manager, L"W32Time", SERVICE_QUERY_STATUS);
    if (!service) {
        result.reason = GetLastError() == ERROR_ACCESS_DENIED ? "W32Time-service-query-permission-denied" : "W32Time-service-unavailable";
        CloseServiceHandle(manager);
        return result;
    }
    SERVICE_STATUS_PROCESS status{};
    DWORD returned = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &returned))
        result.reason = "W32Time-service-status-unavailable";
    else if (status.dwCurrentState != SERVICE_RUNNING)
        result.reason = "W32Time-not-running; synchronization-unverified";
    else result.reason = "W32Time-running; service-configuration-does-not-prove-synchronization";
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    // Windows exposes no supported simple native equivalent of ntp_gettime.
    // A running/enabled service is not sufficient evidence to set synchronized.
    return result;
}

namespace {
std::atomic<bool> cancellation_active{false}, cancellation_console{false};
volatile std::sig_atomic_t cancellation_signal = 0;
void cancel_signal(int signal) { cancellation_signal = signal; }
BOOL WINAPI cancel_control(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT &&
        event != CTRL_LOGOFF_EVENT && event != CTRL_SHUTDOWN_EVENT) return FALSE;
    cancellation_console.store(true);
    return TRUE;
}
constexpr size_t polling_state_limit = 1024 * 1024;
void private_handle(HANDLE handle, bool directory, PSID expected_owner) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory || (!directory && info.nNumberOfLinks != 1))
        throw std::runtime_error("Polling storage requires ordinary files and directories without reparse points");
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                        &owner, nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS)
        throw std::runtime_error("Unable to verify private polling permissions");
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool valid = owner && EqualSid(owner, expected_owner) && acl && acl->AceCount == 1 &&
                 GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED);
    if (valid) {
        void* raw = nullptr;
        valid = GetAce(acl, 0, &raw) != FALSE;
        if (valid) {
            const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
            valid = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && ace->Header.AceFlags == 0 &&
                    (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS &&
                    EqualSid(const_cast<DWORD*>(&ace->SidStart), expected_owner);
        }
    }
    LocalFree(descriptor);
    if (!valid) throw std::runtime_error("Polling storage requires private owner-only permissions");
}
}

struct Cancellation::Impl {
    using Handler = void (*)(int);
    Handler old_int = SIG_DFL, old_term = SIG_DFL;
    bool int_set = false, term_set = false, console_set = false;
    void restore() noexcept {
        if (console_set) SetConsoleCtrlHandler(cancel_control, FALSE);
        if (term_set) std::signal(SIGTERM, old_term);
        if (int_set) std::signal(SIGINT, old_int);
        cancellation_active.store(false);
    }
    Impl() {
        if (cancellation_active.exchange(true)) throw std::runtime_error("Cancellation handler already active");
        cancellation_signal = 0;
        cancellation_console.store(false);
        try {
            old_int = std::signal(SIGINT, cancel_signal);
            if (old_int == SIG_ERR) throw std::runtime_error("Unable to install polling cancellation");
            int_set = true;
            old_term = std::signal(SIGTERM, cancel_signal);
            if (old_term == SIG_ERR) throw std::runtime_error("Unable to install polling cancellation");
            term_set = true;
            if (!SetConsoleCtrlHandler(cancel_control, TRUE)) throw std::runtime_error("Unable to install polling console cancellation");
            console_set = true;
        } catch (...) { restore(); throw; }
    }
    ~Impl() { restore(); }
};
Cancellation::Cancellation() : impl_(std::make_unique<Impl>()) {}
Cancellation::~Cancellation() = default;
bool Cancellation::cancelled() const { return cancellation_signal != 0 || cancellation_console.load(); }

struct PollingStorage::Impl {
    std::filesystem::path path;
    PrivateSecurity security;
    // Holding ancestors without delete sharing prevents rename/reparse swaps
    // while path-based Win32 APIs create and atomically replace child files.
    std::vector<std::unique_ptr<Handle>> directories;
    Handle lock;
    PSID owner() const { return reinterpret_cast<const TOKEN_USER*>(security.token_user.data())->User.Sid; }
    explicit Impl(const std::filesystem::path& value) {
        if (value.empty() || value.filename().empty() || value.filename() == L"." || value.filename() == L"..")
            throw std::runtime_error("Invalid polling storage directory");
        for (const auto& part : value) if (part == L"..") throw std::runtime_error("Polling storage cannot traverse parent directories");
        path = std::filesystem::absolute(value);
        if (!supports_private_permissions(path) || !security.initialize())
            throw std::runtime_error("Private polling storage permissions are unavailable");
        auto current = path.root_path();
        const auto relative = path.relative_path();
        for (auto part = relative.begin(); part != relative.end(); ++part) {
            if (*part == L"." || part->empty()) continue;
            current /= *part;
            auto next = part;
            const bool last = ++next == relative.end();
            if (last && !CreateDirectoryW(current.c_str(), &security.attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Unable to create polling storage directory");
            auto handle = std::make_unique<Handle>(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            BY_HANDLE_FILE_INFORMATION info{};
            if (handle->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle->value, &info) ||
                !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("Polling storage ancestor is unavailable or a reparse point");
            if (last) private_handle(handle->value, true, owner());
            directories.push_back(std::move(handle));
        }
        if (directories.empty()) throw std::runtime_error("Invalid polling storage directory");
        lock.value = CreateFileW((path / L"lock").c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
            0, &security.attributes, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (lock.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Polling storage is already locked or unavailable");
        private_handle(lock.value, false, owner());
    }
    void validate() const {
        private_handle(directories.back()->value, true, owner());
        private_handle(lock.value, false, owner());
    }
    std::optional<std::string> read() const {
        validate();
        Handle state(CreateFileW((path / L"budget.json").c_str(), GENERIC_READ | READ_CONTROL,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (state.value == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
            throw std::runtime_error("Unable to read private polling state");
        }
        private_handle(state.value, false, owner());
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(state.value, &size) || size.QuadPart < 0 || size.QuadPart > static_cast<long long>(polling_state_limit))
            throw std::runtime_error("Polling state exceeds size limit or is unreadable");
        std::string contents;
        char buffer[4096];
        for (;;) {
            DWORD count = 0;
            if (!ReadFile(state.value, buffer, sizeof(buffer), &count, nullptr)) throw std::runtime_error("Unable to read polling state");
            if (count == 0) break;
            if (contents.size() + count > polling_state_limit) throw std::runtime_error("Polling state exceeds size limit");
            contents.append(buffer, count);
        }
        return contents;
    }
    void write(const std::string& contents) {
        if (contents.size() > polling_state_limit) throw std::runtime_error("Polling state exceeds size limit");
        (void)read();
        if (!atomic_write_private(path / L"budget.json", contents)) throw std::runtime_error("Unable to persist private polling state");
        // Verify effective ACLs rather than relying solely on requested ACLs.
        if (read() != std::optional<std::string>(contents)) throw std::runtime_error("Polling state replacement could not be verified");
    }
};
PollingStorage::PollingStorage(const std::filesystem::path& directory) : impl_(std::make_unique<Impl>(directory)) {}
PollingStorage::~PollingStorage() = default;
std::optional<std::string> PollingStorage::read() const { return impl_->read(); }
void PollingStorage::write(const std::string& contents) { impl_->write(contents); }

struct TimingGuard::Impl {
    bool attempted = false, priority_changed = false, timer_changed = false;
    int previous = THREAD_PRIORITY_NORMAL;
    void activate() {
        if (attempted) return;
        attempted = true;
        previous = GetThreadPriority(GetCurrentThread());
        if (previous != THREAD_PRIORITY_ERROR_RETURN && previous < THREAD_PRIORITY_ABOVE_NORMAL)
            priority_changed = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != FALSE;
        timer_changed = timeBeginPeriod(1) == TIMERR_NOERROR;
    }
    ~Impl() {
        if (priority_changed) SetThreadPriority(GetCurrentThread(), previous);
        if (timer_changed) timeEndPeriod(1);
    }
};
TimingGuard::TimingGuard() : impl_(std::make_unique<Impl>()) {}
TimingGuard::~TimingGuard() = default;
void TimingGuard::activate() { impl_->activate(); }
void cpu_relax() { _mm_pause(); }
const char* user_agent() {
    return "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36";
}
const char* browser_platform() { return "\"Windows\""; }
} // namespace itu::platform
