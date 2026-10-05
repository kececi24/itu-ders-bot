#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <system_error>

#include <include/nlohmann_json.hpp>
#include <include/console.hpp>
#include <include/polling_config.hpp>

using json = nlohmann::json;

std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool replace_file(const std::string& path, const std::string& contents) {
    if (!itu::platform::atomic_write_private(std::filesystem::u8path(path), contents)) {
        std::cerr << "Failed to replace: " << path << '\n';
        return false;
    }
    return true;
}

bool encode_env_value(const std::string& value, std::string& encoded) {
    if (value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) return false;
    // The loader removes exactly one pair of outer quotes, without unescaping.
    // Therefore inner quotes, backslashes and whitespace remain literal.
    encoded = '\"' + value + '\"';
    return true;
}

bool write_to_env(const std::string& path, const std::string& username, const std::string& password) {
    std::string encoded_username, encoded_password;
    if (!encode_env_value(username, encoded_username) || !encode_env_value(password, encoded_password)) {
        std::cerr << "Credentials contain characters that cannot be represented in .env\n";
        return false;
    }
    const auto native_path = std::filesystem::u8path(path);
    std::ifstream file(native_path);
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(native_path, status_error);
    if (!file && (status.type() != std::filesystem::file_type::not_found ||
                  (status_error && status_error != std::errc::no_such_file_or_directory))) {
        std::cerr << "Failed to read: " << path << "\n";
        return false;
    }
    std::ostringstream output;
    std::string line;
    while (std::getline(file, line)) {
        std::string entry = trim(line);
        if (entry.rfind("export ", 0) == 0) entry = trim(entry.substr(7));
        auto eq = entry.find('=');
        std::string key = eq == std::string::npos ? "" : trim(entry.substr(0, eq));
        if (key != "ITU_USERNAME" && key != "ITU_PASSWORD") output << line << '\n';
    }
    if (file.bad()) {
        std::cerr << "Failed to read: " << path << "\n";
        return false;
    }
    file.close();
    output << "ITU_USERNAME=" << encoded_username << '\n'
           << "ITU_PASSWORD=" << encoded_password << '\n';
    return replace_file(path, output.str());
}

bool update_user(const std::string& path) {
    std::string username, password;
    std::cout << "Enter your username: ";
    if (!terminal::read_line(username)) return false;
    username = trim(username);
    if (!terminal::read_line(password, true, "Enter your password: ")) return false;
    std::cout << '\n';
    if (username.empty() || password.empty()) {
        std::cerr << "Username and password cannot be empty\n";
        return false;
    }
    if (!write_to_env(path, username, password)) return false;
    std::cout << "User credentials updated successfully\n";
    return true;
}

bool parse_date(const std::string& input, int& year, int& month, int& day) {
    char first = 0, second = 0;
    std::istringstream stream(input);
    if (!(stream >> year >> first >> month >> second >> day) || first != '/' || second != '/') return false;
    stream >> std::ws;
    if (!stream.eof() || year < 1 || month < 1 || month > 12) return false;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = days[month - 1];
    if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) max_day = 29;
    return day >= 1 && day <= max_day;
}

bool parse_time(const std::string& input, int& hour, int& minute, int& second, int& millisecond) {
    char first = 0, middle = 0, last = 0;
    std::istringstream stream(input);
    if (!(stream >> hour >> first >> minute >> middle >> second >> last >> millisecond) ||
        first != ':' || middle != ':' || last != ':') return false;
    stream >> std::ws;
    return stream.eof() && hour >= 0 && hour < 24 && minute >= 0 && minute < 60 &&
           second >= 0 && second < 60 && millisecond >= 0 && millisecond < 1000;
}

std::vector<std::string> parse_crns(const std::string& input) {
    std::vector<std::string> result;
    std::istringstream stream(input);
    std::string crn;
    while (std::getline(stream, crn, ',')) {
        crn = trim(crn);
        if (!crn.empty()) result.push_back(crn);
    }
    return result;
}

bool load_config_for_update(const std::string& path, json& data) {
    const auto native_path = std::filesystem::u8path(path);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(native_path, error);
    if (status.type() == std::filesystem::file_type::not_found &&
        (!error || error == std::errc::no_such_file_or_directory)) {
        data = json::object();
        return true;
    }
    std::ifstream file(native_path);
    if (error || !file) {
        std::cerr << "Failed to read existing configuration\n";
        return false;
    }
    std::vector<std::set<std::string>> keys;
    bool duplicate = false;
    data = json::parse(file, [&](int, json::parse_event_t event, json& parsed) {
        if (event == json::parse_event_t::object_start) keys.emplace_back();
        else if (event == json::parse_event_t::key && !keys.empty()) {
            if (!keys.back().insert(parsed.get<std::string>()).second) duplicate = true;
        } else if (event == json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
        return true;
    }, false);
    if (file.bad() || duplicate || data.is_discarded() || !data.is_object() ||
        (data.contains("time") && !data.at("time").is_object()) ||
        (data.contains("courses") && !data.at("courses").is_object())) {
        std::cerr << "Existing configuration is malformed; no changes were written\n";
        return false;
    }
    return true;
}

bool write_config_update(const std::string& path, json data, const json& time, const json& courses) {
    if (!data.contains("time")) data["time"] = json::object();
    if (!data.contains("courses")) data["courses"] = json::object();
    data["time"].update(time);
    data["courses"].update(courses);
    // Keep existing legacy aliases consistent when setup updates canonical fields.
    if (data["time"].contains("milisecond")) data["time"]["milisecond"] = time.at("millisecond");
    if (data["time"].contains("lead_milisecond")) data["time"]["lead_milisecond"] = time.at("lead_millisecond");
    try {
        (void)itu::polling::parse(data);
    } catch (const std::exception&) {
        std::cerr << "Configuration conflicts with polling settings; no changes were written\n";
        return false;
    }
    return replace_file(path, data.dump(4));
}

bool update_config(const std::string& path) {
    json data;
    if (!load_config_for_update(path, data)) return false;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int millisecond = 0, lead_millisecond = 0;
    std::string date, time, lead_input, addlist, droplist;
    std::cout << "Enter date for course selection (YYYY/MM/DD): ";
    if (!terminal::read_line(date) || !parse_date(date, year, month, day)) {
        std::cerr << "Invalid date\n";
        return false;
    }
    std::cout << "Enter time of course selection (HH:MM:SS:MS): ";
    if (!terminal::read_line(time) || !parse_time(time, hour, minute, second, millisecond)) {
        std::cerr << "Invalid time\n";
        return false;
    }
    std::cout << "Enter lead milliseconds (0 for none): ";
    if (!terminal::read_line(lead_input)) return false;
    std::istringstream lead_stream(lead_input);
    if (!(lead_stream >> lead_millisecond)) {
        std::cerr << "Invalid lead milliseconds\n";
        return false;
    }
    lead_stream >> std::ws;
    if (!lead_stream.eof() || lead_millisecond < 0) {
        std::cerr << "Invalid lead milliseconds\n";
        return false;
    }
    std::cout << "Enter add CRNs (separate by comma): ";
    if (!terminal::read_line(addlist)) return false;
    std::cout << "Enter drop CRNs (separate by comma): ";
    if (!terminal::read_line(droplist)) return false;

    const json updated_time = {
        {"year", year}, {"month", month}, {"day", day}, {"hour", hour},
        {"minute", minute}, {"second", second}, {"millisecond", millisecond},
        {"lead_millisecond", lead_millisecond}
    };
    const json updated_courses = {
        {"crn", parse_crns(addlist)}, {"scrn", parse_crns(droplist)}
    };
    if (!write_config_update(path, std::move(data), updated_time, updated_courses)) return false;
    std::cout << "Config file updated successfully\n";
    return true;
}

int run_setup(int argc, char** argv) {
    std::string configpath = "data/config.json";
    std::string envpath = ".env";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config-path" || arg == "--env-path") {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                std::cerr << "Missing value for " << arg << '\n';
                return 1;
            }
            (arg == "--config-path" ? configpath : envpath) = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }
    itu::platform::ConsoleSession console;
    std::vector<std::string> options = {
        "1. Update user credentials",
        "2. Update add/drop list and time",
        "3. Exit"
    };
    int choice = show_menu("(Use arrow keys and Enter)", options);
    if (choice == 0) return update_user(envpath) ? 0 : 1;
    if (choice == 1) return update_config(configpath) ? 0 : 1;
    return 0;
}

int main(int argc, char** argv) {
    try {
        auto utf8 = itu::platform::arguments(argc, argv);
        std::vector<char*> pointers;
        for (auto& argument : utf8) pointers.push_back(argument.data());
        pointers.push_back(nullptr);
        return run_setup(static_cast<int>(utf8.size()), pointers.data());
    }
    catch (const std::exception& error) {
        std::cerr << "Setup failed: " << error.what() << '\n';
        return 1;
    }
}
