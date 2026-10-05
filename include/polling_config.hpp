#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "nlohmann_json.hpp"

namespace itu::polling {

using WallClock = std::chrono::system_clock;
enum class Distribution { beta, uniform };

struct Config {
    bool enabled = false;
    WallClock::time_point start{}, end{};
    Distribution distribution = Distribution::beta;
    std::chrono::milliseconds min_interval{3000}, max_interval{0}, expected_interval{0};
    double beta_concentration = 6;
    struct RequestBudget {
        std::uint64_t count = 0;
        std::chrono::seconds window{0};
    } request_budget;
    std::uint64_t max_attempts = 0;
    std::chrono::milliseconds backoff_base{30000}, backoff_max{300000};
    std::vector<std::string> crns;
};

// Errors contain field names only; configuration values can contain personal data.
inline void invalid(const char* field) {
    throw std::runtime_error(std::string("Invalid polling configuration: ") + field);
}

inline WallClock::time_point parse_rfc3339(const std::string& value) {
    const auto bad = []() { invalid("start/end must be representable RFC 3339 timestamps with an explicit offset"); };
    const auto digits = [&](std::size_t at, std::size_t count) {
        if (at + count > value.size()) { bad(); return 0; }
        int result = 0;
        for (std::size_t i = at; i < at + count; ++i) {
            if (value[i] < '0' || value[i] > '9') { bad(); return 0; }
            result = result * 10 + value[i] - '0';
        }
        return result;
    };
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        (value[10] != 'T' && value[10] != 't') || value[13] != ':' || value[16] != ':') bad();
    const int year = digits(0, 4), month = digits(5, 2), day = digits(8, 2);
    const int hour = digits(11, 2), minute = digits(14, 2), second = digits(17, 2);
    const int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 1 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) bad();
    const bool leap = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
    if (day < 1 || day > month_days[month - 1] + (month == 2 && leap ? 1 : 0)) bad();
    std::size_t position = 19;
    std::int64_t nanos = 0;
    if (position < value.size() && value[position] == '.') {
        const auto first = ++position;
        while (position < value.size() && value[position] >= '0' && value[position] <= '9') {
            if (position - first >= 9) bad();
            nanos = nanos * 10 + value[position++] - '0';
        }
        if (position == first) bad();
        for (std::size_t i = position - first; i < 9; ++i) nanos *= 10;
    }
    int offset = 0;
    if (position < value.size() && (value[position] == 'Z' || value[position] == 'z')) {
        if (++position != value.size()) bad();
    } else {
        if (position + 6 != value.size() || (value[position] != '+' && value[position] != '-') || value[position + 3] != ':') bad();
        const int offset_hour = digits(position + 1, 2), offset_minute = digits(position + 4, 2);
        if (offset_hour > 23 || offset_minute > 59) bad();
        offset = offset_hour * 3600 + offset_minute * 60;
        // RFC 3339 -00:00 denotes an unknown offset, not a known UTC reference.
        if (value[position] == '-') { if (!offset) bad(); offset = -offset; }
    }
    // Gregorian civil date -> epoch days; independent of host timezone and time_t width.
    const int adjusted_year = year - (month <= 2);
    const int era = adjusted_year / 400;
    const unsigned yoe = static_cast<unsigned>(adjusted_year - era * 400);
    const unsigned shifted_month = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned doy = (153 * shifted_month + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    const std::int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second - offset;
    using Duration = WallClock::duration;
    // Retain one second of headroom at each native duration boundary before casting.
    const auto low = std::chrono::duration_cast<std::chrono::seconds>(Duration::min()).count();
    const auto high = std::chrono::duration_cast<std::chrono::seconds>(Duration::max()).count();
    if (seconds <= low || seconds >= high) bad();
    const auto fraction = std::chrono::duration_cast<Duration>(std::chrono::nanoseconds(nanos));
    if (std::chrono::duration_cast<std::chrono::nanoseconds>(fraction).count() != nanos) bad();
    return WallClock::time_point(std::chrono::duration_cast<Duration>(std::chrono::seconds(seconds)) + fraction);
}

inline std::uint64_t positive_integer(const nlohmann::json& object, const char* field, std::uint64_t maximum) {
    if (!object.contains(field)) invalid(field);
    const auto& value = object.at(field);
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() <= 0)) invalid(field);
    const auto result = value.get<std::uint64_t>();
    if (!result || result > maximum) invalid(field);
    return result;
}

inline Config parse(const nlohmann::json& full_config) {
    Config config;
    if (!full_config.is_object()) invalid("root must be an object");
    if (!full_config.contains("polling")) return config;
    const auto& input = full_config.at("polling");
    if (!input.is_object()) invalid("polling must be an object");
    if (input.contains("enabled")) {
        if (!input.at("enabled").is_boolean()) invalid("enabled must be boolean");
        config.enabled = input.at("enabled").get<bool>();
    }
    if (!config.enabled) return config;
    for (const auto* field : {"start", "end"})
        if (!input.contains(field) || !input.at(field).is_string()) invalid(field);
    config.start = parse_rfc3339(input.at("start").get<std::string>());
    config.end = parse_rfc3339(input.at("end").get<std::string>());
    if (config.start >= config.end) invalid("start must precede end");
    const long double window_ticks = static_cast<long double>(config.end.time_since_epoch().count()) -
                                     static_cast<long double>(config.start.time_since_epoch().count());
    if (window_ticks > static_cast<long double>(WallClock::duration::max().count()) / 4) invalid("window duration");
    const auto wall_limit = std::chrono::duration_cast<std::chrono::milliseconds>(WallClock::duration::max()).count() / 4;
    const auto steady_limit = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::duration::max()).count() / 4;
    const auto limit = static_cast<std::uint64_t>(wall_limit < steady_limit ? wall_limit : steady_limit);
    const auto duration = [&](const char* field, std::chrono::milliseconds fallback, bool required = false) {
        if (!input.contains(field) && !required) return fallback;
        return std::chrono::milliseconds(positive_integer(input, field, limit));
    };
    config.min_interval = duration("min_interval_ms", config.min_interval);
    config.max_interval = duration("max_interval_ms", {}, true);
    config.expected_interval = duration("expected_interval_ms", {}, true);
    config.backoff_base = duration("backoff_base_ms", config.backoff_base);
    config.backoff_max = duration("backoff_max_ms", config.backoff_max);
    if (config.min_interval.count() < 3000 || config.min_interval >= config.max_interval ||
        config.expected_interval <= config.min_interval || config.expected_interval >= config.max_interval) invalid("interval bounds/expected value");
    if (config.backoff_base > config.backoff_max) invalid("backoff bounds");
    if (input.contains("distribution")) {
        if (!input.at("distribution").is_string()) invalid("distribution");
        const auto name = input.at("distribution").get<std::string>();
        if (name == "uniform") config.distribution = Distribution::uniform;
        else if (name != "beta") invalid("distribution");
    }
    if (input.contains("beta_concentration")) {
        if (!input.at("beta_concentration").is_number()) invalid("beta_concentration");
        config.beta_concentration = input.at("beta_concentration").get<double>();
    }
    if (!std::isfinite(config.beta_concentration) || config.beta_concentration <= 0) invalid("beta_concentration");
    if (config.distribution == Distribution::uniform &&
        config.expected_interval - config.min_interval != config.max_interval - config.expected_interval) invalid("uniform expected value must equal midpoint");
    const double p = static_cast<double>((config.expected_interval - config.min_interval).count()) /
                     static_cast<double>((config.max_interval - config.min_interval).count());
    if (!(config.beta_concentration * p > 0) || !(config.beta_concentration * (1 - p) > 0)) invalid("beta shape parameters");
    if (!input.contains("request_budget") || !input.at("request_budget").is_object()) invalid("request_budget");
    const auto& budget = input.at("request_budget");
    config.request_budget.count = positive_integer(budget, "count", static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    config.request_budget.window = std::chrono::seconds(positive_integer(budget, "window_seconds", limit / 1000));
    config.max_attempts = positive_integer(input, "max_attempts", static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    if (!full_config.contains("courses") || !full_config.at("courses").is_object()) invalid("courses");
    const auto& courses = full_config.at("courses");
    if (!courses.contains("crn") || !courses.at("crn").is_array() || courses.at("crn").empty()) invalid("nonempty add CRNs required");
    std::set<std::string> unique;
    for (const auto& item : courses.at("crn")) {
        if (!item.is_string()) invalid("CRNs must be strings");
        const auto crn = item.get<std::string>();
        if (crn.empty() || crn.find_first_not_of("0123456789") != std::string::npos || !unique.insert(crn).second) invalid("CRNs must be nonempty unique digit strings");
        config.crns.push_back(crn);
    }
    if (courses.contains("scrn") && (!courses.at("scrn").is_array() || !courses.at("scrn").empty())) invalid("drop operations cannot be polled");
    if (full_config.contains("time")) {
        const auto& time = full_config.at("time");
        if (!time.is_object()) invalid("time");
        for (const auto* field : {"lead_millisecond", "lead_milisecond"}) {
            if (time.contains(field) && (!time.at(field).is_number_integer() || time.at(field) != 0)) invalid("polling requires zero lead");
        }
    }
    return config;
}

} // namespace itu::polling
