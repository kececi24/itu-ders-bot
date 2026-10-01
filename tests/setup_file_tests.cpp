#define main setup_application_main
#include "setup/main.cpp"
#undef main

#include <cassert>
#include <filesystem>
#include "test_helpers.hpp"

static std::string read_file(const std::string& path) {
    std::ifstream file(test_helpers::path(path));
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main() {
    test_helpers::TemporaryDirectory directory;
    const std::string root = directory.root.u8string();
    const std::string path = root + "/.env";
    { std::ofstream out(test_helpers::path(path)); out << "# preserved\nOTHER=value\nITU_USERNAME=old\nexport ITU_USERNAME=duplicate\nITU_PASSWORD=old\n"; }
    const std::string password = "  both'\"quotes\\ #=literal  ";
    assert(write_to_env(path, "user", password));
    auto content = read_file(path);
    assert(content == "# preserved\nOTHER=value\nITU_USERNAME=\"user\"\nITU_PASSWORD=\"" + password + "\"\n");
    auto position = content.find("ITU_PASSWORD=") + std::string("ITU_PASSWORD=").size();
    std::string encoded = content.substr(position, content.size() - position - 1);
    assert(encoded.substr(1, encoded.size() - 2) == password);
    test_helpers::private_file(test_helpers::path(path));
    const auto fresh = root + "/fresh.env";
    assert(write_to_env(fresh, "üser", password));
    test_helpers::private_file(test_helpers::path(fresh));
    assert(!write_to_env(path, "user", "bad\npassword"));
    assert(read_file(path) == content);
#ifndef _WIN32
    // POSIX NAME_MAX: destination exists, but appending the temporary suffix exceeds NAME_MAX.
    const auto long_path = root + "/" + std::string(250, 'x');
    { std::ofstream out(test_helpers::path(long_path)); out << "preserve"; }
    assert(!replace_file(long_path, "replacement"));
    assert(read_file(long_path) == "preserve");
#endif
    const auto destination_directory = root + "/existing";
    std::filesystem::create_directory(test_helpers::path(destination_directory));
    assert(!replace_file(destination_directory, "replacement"));
    assert(std::filesystem::is_directory(test_helpers::path(destination_directory)));
#ifdef __APPLE__
    const auto acl_dir = root + "/acl_inherited";
    std::filesystem::create_directory(test_helpers::path(acl_dir));
    int chmod_res = std::system(("chmod +a 'everyone allow read,file_inherit' \"" + acl_dir + "\"").c_str());
    if (chmod_res == 0) {
        const auto acl_env = acl_dir + "/.env";
        assert(write_to_env(acl_env, "user", "secret_one"));
        assert(read_file(acl_env).find("secret_one") != std::string::npos);
        test_helpers::private_file(test_helpers::path(acl_env));

        assert(write_to_env(acl_env, "user", "secret_two"));
        assert(read_file(acl_env).find("secret_two") != std::string::npos);
        test_helpers::private_file(test_helpers::path(acl_env));

        const std::string preserved_content = read_file(acl_env);
        const auto sub_dir = acl_dir + "/sub";
        std::filesystem::create_directory(test_helpers::path(sub_dir));
        assert(!replace_file(sub_dir, "cannot_overwrite_dir"));
        assert(std::filesystem::is_directory(test_helpers::path(sub_dir)));
        assert(read_file(acl_env) == preserved_content);

        for (auto& entry : std::filesystem::directory_iterator(test_helpers::path(acl_dir)))
            assert(entry.path().filename().string().find(".tmp.") == std::string::npos);
    }
#endif
    for (auto& entry : std::filesystem::directory_iterator(directory.root))
        assert(entry.path().filename().string().find(".tmp.") == std::string::npos);
    assert(!write_to_env(root, "user", "password"));
    int y, m, d, h, s, ms;
    assert(parse_date("2028/02/29", y, m, d));
    assert(!parse_date("2027/02/29", y, m, d));
    assert(parse_time("23:59:59:999", h, m, s, ms));
    assert(!parse_time("24:00:00:000", h, m, s, ms));
    assert((parse_crns(" 123, ,456,123 ") == std::vector<std::string>{"123", "456", "123"}));
    
}
