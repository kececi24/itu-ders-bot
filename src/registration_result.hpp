#pragma once

#include "http.hpp"
#include "include/nlohmann_json.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace itu::polling {

enum class Category { success, business_rejection, action_required, rate_limited, http_error,
                      infrastructure_failure, unexpected_redirect, malformed, unknown_application };
enum class Action { stop_success, retry_normal, stop_action_required, stop_rate_limit, stop_unknown };
struct Result {
    Category category = Category::malformed;
    Action action = Action::stop_unknown;
    bool complete = false;
    std::vector<std::string> satisfied, pending, codes;
};

inline const char* category_name(Category value) {
    switch (value) {
    case Category::success: return "success";
    case Category::business_rejection: return "business_rejection";
    case Category::action_required: return "action_required";
    case Category::rate_limited: return "rate_limited";
    case Category::http_error: return "http_error";
    case Category::infrastructure_failure: return "infrastructure_failure";
    case Category::unexpected_redirect: return "unexpected_redirect";
    case Category::malformed: return "malformed";
    case Category::unknown_application: return "unknown_application";
    }
    return "unknown";
}
inline const char* action_name(Action value) {
    switch (value) {
    case Action::stop_success: return "stop_success";
    case Action::retry_normal: return "retry_normal";
    case Action::stop_action_required: return "stop_action_required";
    case Action::stop_rate_limit: return "stop_rate_limit";
    case Action::stop_unknown: return "stop_unknown";
    }
    return "stop_unknown";
}

inline Result classify(const HttpResponse& response, const std::vector<std::string>& submitted) {
    Result result;
    result.pending = submitted;
    if (response.status == 429) {
        result.category = Category::rate_limited;
        result.action = Action::stop_rate_limit;
        return result;
    }
    if (response.status >= 500 && response.status <= 599) { result.category = Category::infrastructure_failure; return result; }
    if (response.status >= 300 && response.status <= 399) { result.category = Category::unexpected_redirect; return result; }
    if (response.status < 200 || response.status >= 300) { result.category = Category::http_error; return result; }
    const std::set<std::string> expected(submitted.begin(), submitted.end());
    if (expected.empty() || expected.size() != submitted.size() || expected.count("")) return result;
    // Refuse duplicate JSON keys rather than allowing the parser's last value to
    // decide whether a potentially applied registration can safely be repeated.
    std::vector<std::set<std::string>> keys;
    bool duplicate_key = false;
    const auto document = nlohmann::json::parse(response.body, [&](int, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
        if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
        else if (event == nlohmann::json::parse_event_t::key && !keys.empty()) {
            if (!keys.back().insert(parsed.get<std::string>()).second) duplicate_key = true;
        } else if (event == nlohmann::json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
        return true;
    }, false);
    if (duplicate_key || document.is_discarded() || !document.is_object() ||
        !document.contains("ecrnResultList") || !document.at("ecrnResultList").is_array()) return result;
    enum class Entry { satisfied, retryable, action_required, unknown };
    const std::set<std::string> actions{"VAL04", "VAL05", "VAL07", "VAL08", "VAL09", "VAL10", "VAL11", "VAL12",
                                        "VAL13", "VAL15", "VAL18", "VAL19", "VAL20", "CRNListEmpty", "CRNNotFound"};
    const std::set<std::string> ambiguous{"errorResult", "None", "error", "VAL01", "VAL14", "VAL16", "VAL21", "ERRLoad",
        "NULLParam-CheckOgrenciKayitZamaniKontrolu", "Ekleme İşlemi Başarılı", "Kontenjan Dolu", "Silme İşlemi Başarılı"};
    std::map<std::string, std::vector<Entry>> entries;
    bool malformed = document.contains("scrnResultList") &&
                     (!document.at("scrnResultList").is_array() || !document.at("scrnResultList").empty());
    bool unknown = false, action_required = false;
    for (const auto& item : document.at("ecrnResultList")) {
        if (!item.is_object() || !item.contains("crn") || !item.at("crn").is_string()) { malformed = true; continue; }
        const auto crn = item.at("crn").get<std::string>();
        if (!item.contains("resultCode") || !item.at("resultCode").is_string()) {
            malformed = true;
            entries[crn].push_back(Entry::unknown);
            continue;
        }
        const auto code = item.at("resultCode").get<std::string>();
        Entry entry = Entry::unknown;
        if (code == "successResult" || code == "VAL03") entry = Entry::satisfied;
        else if (code == "VAL06" || code == "VAL02") entry = Entry::retryable;
        else if (actions.count(code)) entry = Entry::action_required;
        if (entry != Entry::unknown || ambiguous.count(code)) result.codes.push_back(code);
        if (!expected.count(crn)) malformed = true;
        entries[crn].push_back(entry);
    }
    result.pending.clear();
    for (const auto& crn : submitted) {
        const auto found = entries.find(crn);
        if (found == entries.end() || found->second.size() != 1) {
            malformed = true;
            result.pending.push_back(crn);
            continue;
        }
        switch (found->second.front()) {
        case Entry::satisfied: result.satisfied.push_back(crn); break;
        case Entry::retryable: result.pending.push_back(crn); break;
        case Entry::action_required: result.pending.push_back(crn); action_required = true; break;
        case Entry::unknown: result.pending.push_back(crn); unknown = true; break;
        }
    }
    result.complete = !malformed;
    if (malformed) return result;
    if (unknown) { result.category = Category::unknown_application; return result; }
    if (action_required) { result.category = Category::action_required; result.action = Action::stop_action_required; return result; }
    result.category = result.pending.empty() ? Category::success : Category::business_rejection;
    result.action = result.pending.empty() ? Action::stop_success : Action::retry_normal;
    return result;
}

} // namespace itu::polling
