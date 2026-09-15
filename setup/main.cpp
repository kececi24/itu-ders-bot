#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <windows.h>

#include <include/nlohmann_json.hpp>
#include <include/console.hpp>

using json = nlohmann::json;

std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool replace_file(const std::string& path, const std::string& contents) {
    std::string temporary = path + ".tmp." + std::to_string(GetCurrentProcessId());
    bool written = false;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (file && (file << contents) && (file.flush())) written = true;
    }
    if (!written) {
        std::cerr << "Failed to write: " << temporary << "\n";
        DeleteFileA(temporary.c_str());
        return false;
    }
    if (!MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::cerr << "Failed to replace: " << path << "\n";
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}

bool encode_env_value(const std::string& value, std::string& encoded) {
    if (value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) return false;
    bool quoted = value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''));
    if (trim(value) == value && !value.empty() && value[0] != '#' && !quoted) {
        encoded = value;
        return true;
    }
    if (value.find('"') == std::string::npos) {
        encoded = '"' + value + '"';
        return true;
    }
    if (value.find('\'') == std::string::npos) {
        encoded = '\'' + value + '\'';
        return true;
    }
    return false;
}

bool write_to_env(const std::string& path, const std::string& username, const std::string& password) {
    std::string encoded_username, encoded_password;
    if (!encode_env_value(username, encoded_username) || !encode_env_value(password, encoded_password)) {
        std::cerr << "Credentials contain characters that cannot be represented in .env\n";
        return false;
    }
    std::ifstream file(path);
    if (!file && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
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

struct EchoGuard {
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    bool changed = false;
    EchoGuard() {
        if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode))
            changed = SetConsoleMode(handle, mode & ~ENABLE_ECHO_INPUT) != 0;
    }
    ~EchoGuard() {
        if (changed) SetConsoleMode(handle, mode);
    }
};

bool update_user(const std::string& path) {
    std::string username, password;
    std::cout << "Enter your username: ";
    if (!std::getline(std::cin, username)) return false;
    username = trim(username);
    std::cout << "Enter your password: " << std::flush;
    {
        EchoGuard echo;
        if (!echo.changed) {
            std::cerr << "Unable to disable password echo\n";
            return false;
        }
        if (!std::getline(std::cin, password)) return false;
    }
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

bool update_config(const std::string& path) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int millisecond = 0, lead_millisecond = 0;
    std::string date, time, lead_input, addlist, droplist;
    std::cout << "Enter date for course selection (YYYY/MM/DD): ";
    if (!std::getline(std::cin, date) || !parse_date(date, year, month, day)) {
        std::cerr << "Invalid date\n";
        return false;
    }
    std::cout << "Enter time of course selection (HH:MM:SS:MS): ";
    if (!std::getline(std::cin, time) || !parse_time(time, hour, minute, second, millisecond)) {
        std::cerr << "Invalid time\n";
        return false;
    }
    std::cout << "Enter lead milliseconds (0 for none): ";
    if (!std::getline(std::cin, lead_input)) return false;
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
    if (!std::getline(std::cin, addlist)) return false;
    std::cout << "Enter drop CRNs (separate by comma): ";
    if (!std::getline(std::cin, droplist)) return false;

    json data;
    data["time"] = {
        {"year", year}, {"month", month}, {"day", day}, {"hour", hour},
        {"minute", minute}, {"second", second}, {"millisecond", millisecond},
        {"lead_millisecond", lead_millisecond}
    };
    data["courses"] = {
        {"crn", parse_crns(addlist)}, {"scrn", parse_crns(droplist)}
    };
    if (!replace_file(path, data.dump(4))) return false;
    std::cout << "Config file updated successfully\n";
    return true;
}

int main(int argc, char** argv) {
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
    enable_ansi();
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
