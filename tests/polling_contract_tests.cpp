#include "include/polling_config.hpp"
#include "src/registration_result.hpp"
#include <iostream>
#include <stdexcept>

using nlohmann::json;
using namespace itu::polling;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <typename Operation>
static void refuses(Operation operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
static json valid_config() {
    return json::parse(R"({"time":{"lead_millisecond":0},"courses":{"crn":["001","002"],"scrn":[]},
        "polling":{"enabled":true,"start":"2028-02-29T12:00:00+03:00","end":"2028-02-29T12:05:00+03:00",
          "max_interval_ms":60000,"expected_interval_ms":40000,"max_attempts":5,
          "request_budget":{"count":20,"window_seconds":3600}}})");
}
static HttpResponse response(const json& entries, long status = 200) {
    return {status, json{{"ecrnResultList", entries}}.dump(), {}, {}};
}
static json entry(const char* crn, const char* code) { return {{"crn", crn}, {"resultCode", code}}; }

int main() {
    try {
        require(!parse(json::object()).enabled, "legacy config stays disabled");
        require(!parse({{"polling", {{"enabled", false}, {"start", "not a date"}}}}).enabled, "disabled settings have no operating effect");
        const auto original = valid_config();
        const auto config = parse(original);
        require(config.enabled && config.crns == std::vector<std::string>({"001", "002"}), "ordered add CRNs preserve leading zeroes");
        require(config.distribution == Distribution::beta && config.min_interval.count() == 3000 && config.beta_concentration == 6, "safe non-rate defaults");
        require(config.backoff_base.count() == 30000 && config.backoff_max.count() == 300000, "backoff defaults");
        require(config.end - config.start == std::chrono::minutes(5), "absolute interval");
        require(config.start == parse_rfc3339("2028-02-29T09:00:00Z"), "offset converted independently of local timezone");
        require(parse_rfc3339("2028-02-29t09:00:00.125z") - config.start == std::chrono::milliseconds(125), "fractional timestamp");
        require(parse_rfc3339("1970-01-01T00:00:00Z").time_since_epoch().count() == 0, "epoch anchor");
        require(parse_rfc3339("1969-12-31T23:59:59Z").time_since_epoch() == -std::chrono::seconds(1), "negative epoch anchor");
        for (const auto* date : {"2027-02-29T12:00:00Z", "2028-04-31T12:00:00Z", "2028-00-01T12:00:00Z", "2028-01-00T12:00:00Z",
             "2028-02-29T24:00:00Z", "2028-02-29T12:60:00Z", "2028-02-29T12:00:60Z", "2028-02-29T12:00:00",
             "2028-02-29T12:00:00-00:00", "2028-02-29T12:00:00+24:00", "2028-02-29T12:00:00+03:60", "2028-02-29T12:00:00Ztrailing",
             "2028-02-29T12:00:00.Z", "2028-02-29T12:00:00.1234567890Z", "0000-01-01T00:00:00Z", ""})
            refuses([&] { (void)parse_rfc3339(date); }, "invalid RFC 3339 date must fail");
        for (const auto* field : {"start", "end", "max_interval_ms", "expected_interval_ms", "request_budget", "max_attempts"}) {
            auto bad = original; bad["polling"].erase(field);
            refuses([&] { (void)parse(bad); }, "required polling field");
        }
        for (const auto* field : {"min_interval_ms", "max_interval_ms", "expected_interval_ms", "backoff_base_ms", "backoff_max_ms", "max_attempts"}) {
            for (const json number : {json(-1), json(0), json(1.25), json("5000"), json(true), json(std::numeric_limits<std::uint64_t>::max())}) {
                auto bad = original; bad["polling"][field] = number;
                refuses([&] { (void)parse(bad); }, "invalid duration/count must fail without narrowing");
            }
        }
        for (const auto* field : {"count", "window_seconds"}) {
            for (const json number : {json(-1), json(0), json(1.5), json(std::numeric_limits<std::uint64_t>::max())}) {
                auto bad = original; bad["polling"]["request_budget"][field] = number;
                refuses([&] { (void)parse(bad); }, "invalid budget must fail");
            }
        }
        const auto rejects_change = [&](const char* field, const json& value) {
            auto bad = original; bad["polling"][field] = value;
            refuses([&] { (void)parse(bad); }, "invalid distribution/window bounds");
        };
        rejects_change("min_interval_ms", 2999);
        rejects_change("max_interval_ms", 40000);
        rejects_change("expected_interval_ms", 3000);
        rejects_change("expected_interval_ms", 60000);
        rejects_change("start", original["polling"]["end"]);
        rejects_change("end", "2028-02-29T00:00:00Z");
        rejects_change("distribution", "normal");
        rejects_change("distribution", "uniform");
        rejects_change("beta_concentration", 0);
        rejects_change("beta_concentration", -1);
        rejects_change("beta_concentration", std::numeric_limits<double>::infinity());
        rejects_change("beta_concentration", std::numeric_limits<double>::quiet_NaN());
        rejects_change("backoff_base_ms", 300001);
        auto uniform = original;
        uniform["polling"]["distribution"] = "uniform";
        uniform["polling"]["expected_interval_ms"] = 31500;
        require(parse(uniform).distribution == Distribution::uniform, "uniform midpoint accepted");
        for (const json crns : {json::array(), json::array({"001", "001"}), json::array({""}), json::array({" 001"}), json::array({"001x"}), json::array({1})}) {
            auto bad = original; bad["courses"]["crn"] = crns;
            refuses([&] { (void)parse(bad); }, "invalid add CRNs");
        }
        for (const json drops : {json::array({"003"}), json(""), json(nullptr)}) {
            auto bad = original; bad["courses"]["scrn"] = drops;
            refuses([&] { (void)parse(bad); }, "polling drops are refused");
        }
        for (const auto* lead : {"lead_millisecond", "lead_milisecond"}) {
            auto bad = original; bad["time"][lead] = 1;
            refuses([&] { (void)parse(bad); }, "canonical and alias lead must both be zero");
        }
        const std::vector<std::string> crns{"001", "002"};
        auto result = classify(response({entry("002", "VAL03"), entry("001", "successResult")}), crns);
        require(result.complete && result.action == Action::stop_success && result.satisfied == crns && result.pending.empty(), "success/already-registered stop in submitted order");
        result = classify(response({entry("002", "VAL06"), entry("001", "successResult")}), crns);
        require(result.complete && result.category == Category::business_rejection && result.action == Action::retry_normal &&
                result.satisfied == std::vector<std::string>{"001"} && result.pending == std::vector<std::string>{"002"}, "partial success leaves only residual pending");
        result = classify(response({entry("001", "VAL02"), entry("002", "VAL06")}), crns);
        require(result.action == Action::retry_normal && result.pending == crns, "business rejection has normal retry semantics");
        result = classify(response({entry("001", "VAL04"), entry("002", "successResult")}), crns);
        require(result.action == Action::stop_action_required && result.satisfied == std::vector<std::string>{"002"}, "action required stops while preserving confirmed result");
        for (const auto* code : {"VAL14", "VAL16", "VAL21", "ERRLoad", "arbitrary-secret-server-text"}) {
            result = classify(response({entry("001", code), entry("002", "VAL06")}), crns);
            require(result.complete && result.action == Action::stop_unknown, "uncharacterized response never authorizes replay");
            require(std::find(result.codes.begin(), result.codes.end(), "arbitrary-secret-server-text") == result.codes.end(), "unknown server content excluded from structured result codes");
        }
        for (const json list : {json::array(), json::array({entry("001", "successResult")}),
             json::array({entry("001", "successResult"), entry("001", "VAL06"), entry("002", "VAL06")}),
             json::array({entry("001", "successResult"), json{{"crn", "001"}}, entry("002", "VAL06")}),
             json::array({entry("001", "successResult"), entry("002", "VAL06"), entry("003", "VAL06")})}) {
            result = classify(response(list), crns);
            require(!result.complete && result.action == Action::stop_unknown, "incomplete/conflicting/unexpected aggregate cannot retry");
        }
        result = classify(response({entry("001", "successResult")}), crns);
        require(result.satisfied == std::vector<std::string>{"001"}, "unambiguous success retained in incomplete aggregate");
        result = classify(response({entry("001", "successResult"), entry("001", "VAL06"), entry("002", "VAL06")}), crns);
        require(result.satisfied.empty(), "conflicting duplicate is not unambiguous success");
        for (const auto* body : {"", "{}", "[]", "not-json", "{\"ecrnResultList\":42}",
            "{\"ecrnResultList\":[{\"crn\":\"001\",\"resultCode\":\"successResult\",\"resultCode\":\"VAL06\"},{\"crn\":\"002\",\"resultCode\":\"VAL06\"}]}"}) {
            require(classify(HttpResponse{200, body, {}, {}}, crns).action == Action::stop_unknown, "malformed/duplicate-key JSON cannot retry");
        }
        for (long status : {0, 301, 302, 307, 308, 400, 401, 403, 500, 502, 503, 504}) {
            result = classify(response({entry("001", "VAL06"), entry("002", "VAL06")}, status), crns);
            require(result.action == Action::stop_unknown && !result.complete && result.satisfied.empty(), "HTTP failure never replays POST or trusts body success");
        }
        result = classify(response({}, 429), crns);
        require(result.category == Category::rate_limited && result.action == Action::stop_rate_limit, "explicit HTTP rate limit stops");
        require(classify(response({entry("001", "successResult")}), {"001", "001"}).action == Action::stop_unknown, "invalid submitted set cannot be satisfied");
        std::cout << "Polling configuration and response contracts passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
