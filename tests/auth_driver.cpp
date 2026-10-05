#include "src/token.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string encode(const std::string& input) {
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string output;
    unsigned accumulator = 0, bits = 0;
    for (const unsigned char c : input) {
        accumulator = (accumulator << 8) | c;
        bits += 8;
        while (bits >= 6) { bits -= 6; output += alphabet[(accumulator >> bits) & 63]; }
        accumulator &= (1u << bits) - 1;
    }
    if (bits) output += alphabet[(accumulator << (6 - bits)) & 63];
    return output;
}
void expiry_tests() {
    const auto hint = [](const std::string& payload) { return token_detail::expiry_hint("e30." + encode(payload) + ".signature"); };
    const auto known = hint("{\"exp\":1700000000}");
    check(known && std::chrono::duration_cast<std::chrono::seconds>(known->time_since_epoch()).count() == 1700000000,
          "integer JWT expiry hint missing");
    check(hint("{\"exp\":0}").has_value(), "epoch JWT expiry should be represented as expired");
    for (const auto* payload : {"{}", "[]", "{\"exp\":-1}", "{\"exp\":1.5}", "{\"exp\":\"1700000000\"}",
         "{\"exp\":true}", "{\"exp\":18446744073709551615}", "{\"exp\":1,\"exp\":2}",
         "{\"exp\":null}", "{\"sub\":{\"exp\":1700000000}}", "not-json"})
        check(!hint(payload), "invalid or ambiguous JWT expiry was trusted");
    check(!hint(std::string(20000, 'x')), "oversized JWT parsed");
    check(!token_detail::expiry_hint("e30.A.signature"), "invalid base64 length accepted");
    check(!token_detail::expiry_hint("e30.e31.signature"), "noncanonical base64 trailing bits accepted");
    check(!token_detail::expiry_hint("e30.e30=.signature"), "base64 padding accepted in compact JWT");
}
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        expiry_tests();
        const std::string mode = argv[2];
        if (mode.compare(0, 7, "refresh") == 0) {
            TokenFetcher fetcher(HttpSession::TestOptions{true, ""}, argv[1]);
            std::size_t transfers = 0, requests = 0;
            fetcher.set_observer([&](const HttpRequest& request) {
                check(request.follow_redirects && request.max_redirects == 10, "authentication redirect cap changed");
                ++transfers;
            }, [&](const HttpTransferInfo& info) {
                check(info.request_count.has_value(), "completed authentication count unavailable");
                requests += *info.request_count;
            });
            const auto initial = fetcher.get_token("test ü&+", "fixture-password\t&+", true);
            check(!initial.expires_at, "missing JWT expiry invented");
            check(transfers == 4 && requests == 5, "initial authentication accounting");
            bool expected_failure = false;
            try {
                const auto refreshed = fetcher.refresh_token(true);
                check(mode == "refresh", "refresh unexpectedly succeeded");
                check(refreshed.bearer != initial.bearer && refreshed.expires_at &&
                      std::chrono::duration_cast<std::chrono::seconds>(refreshed.expires_at->time_since_epoch()).count() == 1700000000,
                      "refresh token/expiry missing");
            } catch (const AuthError& error) {
                expected_failure = (mode == "refresh_unauthorized" && error.kind == AuthError::Kind::unauthorized && error.status == 401) ||
                    (mode == "refresh_forbidden" && error.kind == AuthError::Kind::http_failure && error.status == 403) ||
                    (mode == "refresh_limited" && error.kind == AuthError::Kind::http_failure && error.status == 429 && error.retry_after == "3600") ||
                    (mode == "refresh_login" && error.kind == AuthError::Kind::login_required);
                check(expected_failure, "refresh failure classification mismatch");
            }
            check(mode == "refresh" || expected_failure, "missing expected refresh failure");
            check(transfers == 5 && requests == 6, "refresh performed unnecessary login requests");
            return 0;
        }
        HttpSession session(HttpSession::TestOptions{true, ""});
        const auto token = token_detail::fetch(session, argv[1], "test ü&+", "fixture-password\t&+", true);
        if (std::string(argv[2]) != "success" || token != "Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.signature") return 3;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (std::string(argv[2]) != "failure") return 4;
    }
    return 0;
}
