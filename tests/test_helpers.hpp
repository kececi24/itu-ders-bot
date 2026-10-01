#pragma once
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/acl.h>
#endif
#endif
namespace test_helpers {
inline void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline std::filesystem::path path(const std::string& utf8) { return std::filesystem::u8path(utf8); }
inline void environment(const char* name, const char* value) {
#ifdef _WIN32
    const auto wide_name = path(name).wstring();
    const auto wide_value = value ? path(value).wstring() : std::wstring();
    require(_wputenv_s(wide_name.c_str(), wide_value.c_str()) == 0, "set test environment");
#else
    require((value ? setenv(name, value, 1) : unsetenv(name)) == 0, "set test environment");
#endif
}
inline void eastern_timezone() {
#ifdef _WIN32
    environment("TZ", "EST5EDT"); _tzset();
#else
    environment("TZ", "America/New_York"); tzset();
#endif
}
inline std::tm utc(std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    require(gmtime_s(&result, &value) == 0, "UTC conversion");
#else
    require(gmtime_r(&value, &result) != nullptr, "UTC conversion");
#endif
    return result;
}
struct TemporaryDirectory {
    std::filesystem::path root;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            root = std::filesystem::temp_directory_path() / path("itu-test-ü-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(root)) return;
        }
        throw std::runtime_error("create test directory");
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
};
inline void private_file(const std::filesystem::path& file) {
#ifdef _WIN32
    PACL acl = nullptr; PSID owner = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(GetNamedSecurityInfoW(file.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
        &owner, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS, "read private file DACL");
    struct Release { PSECURITY_DESCRIPTOR p; ~Release() { LocalFree(p); } } release{descriptor};
    require(acl != nullptr && owner != nullptr, "private file must have non-null DACL");
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    require(GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED), "private DACL must block inherited access");
    bool owner_access = false;
    BYTE system_buffer[SECURITY_MAX_SID_SIZE], admin_buffer[SECURITY_MAX_SID_SIZE];
    DWORD system_size = sizeof(system_buffer), admin_size = sizeof(admin_buffer);
    require(CreateWellKnownSid(WinLocalSystemSid, nullptr, system_buffer, &system_size) &&
            CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin_buffer, &admin_size), "well-known SID creation");
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void* raw = nullptr; require(GetAce(acl,index,&raw), "read private ACE");
        const auto* header = static_cast<ACE_HEADER*>(raw);
        require(!(header->AceFlags & INHERITED_ACE), "inherited private ACE");
        if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
            auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw); PSID sid = &ace->SidStart;
            require(EqualSid(sid,owner) || EqualSid(sid,system_buffer) || EqualSid(sid,admin_buffer), "private file grants unrelated principal access");
            if (EqualSid(sid,owner) && (ace->Mask & FILE_READ_DATA) && (ace->Mask & FILE_WRITE_DATA)) owner_access = true;
        } else require(header->AceType == ACCESS_DENIED_ACE_TYPE, "unexpected private ACE type");
    }
    require(owner_access, "private file owner lacks read/write access");
#else
    struct stat info{}; require(stat(file.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600 && info.st_uid == geteuid(), "private file mode must be 0600 and owned by current user");
#ifdef __APPLE__
    errno = 0;
    acl_t acl = acl_get_file(file.c_str(), ACL_TYPE_EXTENDED);
    if (acl != nullptr) {
        bool empty = false;
        if (acl_valid(acl) == 0) {
            acl_entry_t entry;
            errno = 0;
            const int result = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
            empty = result == -1 && errno == EINVAL;
        }
        acl_free(acl);
        require(empty, "private file must have a valid empty extended ACL");
    } else require(errno == ENOENT, "read private file extended ACL");
#endif
#endif
}
}
