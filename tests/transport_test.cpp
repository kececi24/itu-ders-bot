#include "src/http.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        check(argc == 5, "fixture URL missing");
        const std::string base = argv[1];
        HttpSession production;
        bool refused = false;
        try { production.prepare({"GET", base}); } catch (...) { refused = true; }
        check(refused, "production accepted cleartext HTTP");
        HttpSession session(HttpSession::TestOptions{true, {}});
        std::size_t reservations = 0;
        HttpTransferInfo last_transfer;
        session.set_observer([&](const HttpRequest& request) {
            reservations += request.follow_redirects ? static_cast<std::size_t>(request.max_redirects + 1) : 1;
        }, [&](const HttpTransferInfo& info) { last_transfer = info; });
        check(session.percent_encode("a&= +/ğ") == "a%26%3D%20%2B%2F%C4%9F", "encoding mismatch");
        check(HttpSession::resolve_url("https://example.org/a/b?x=1", "../login?q=2") == "https://example.org/login?q=2", "URL resolution");
        session.prepare({"GET", base + "/redirect"});
        auto result = session.perform();
        check(result.status == 200 && result.body == "GET|cookie=yes|", "redirect cookie/body failure");
        check(result.effective_url == base + "/echo", "effective URL failure");
        check(result.redirect_count == 1 && last_transfer.request_count == 2 && reservations == 11,
              "redirect accounting failure");
        check(result.headers.count("x-intermediate") == 0 && result.headers.at("x-final") == "yes", "final headers failure");
        HttpSession guarded(HttpSession::TestOptions{true, {}});
        HttpTransferInfo guarded_transfer;
        guarded.set_observer({}, [&](const HttpTransferInfo& info) { guarded_transfer = info; }, true);
        for (const auto* kind : {"positive", "date"}) {
            bool cooldown_stopped = false;
            try { guarded.request({"GET", base + "/redirect-cooldown-" + kind}); }
            catch (const HttpTransportError& error) {
                cooldown_stopped = error.curl_code == 23 && error.http_status == 302 &&
                    error.redirect_count == 0 && !error.proven_pre_dispatch && !error.cancelled &&
                    !guarded_transfer.request_count && !error.retry_after.empty() &&
                    error.retry_after == guarded_transfer.retry_after;
                const std::string diagnostic = error.what();
                check(diagnostic.find("synthetic-secret") == std::string::npos &&
                      diagnostic.find("2099") == std::string::npos && diagnostic.find(base) == std::string::npos,
                      "redirect cooldown diagnostic exposed response metadata");
            }
            check(cooldown_stopped, "redirect followed before its cooldown was observed");
        }
        for (const auto* kind : {"zero", "past", "invalid"}) {
            const auto completed = guarded.request({"GET", base + "/redirect-cooldown-" + kind});
            check(completed.status == 200 && completed.redirect_count == 1 && guarded_transfer.request_count == 2,
                  "inactive or invalid Retry-After stopped a normal redirect");
        }
        const auto unfollowed = guarded.request({"GET", base + "/redirect-cooldown-positive", {}, {}, 1000, false});
        check(unfollowed.status == 302 && guarded_transfer.retry_after == "120",
              "no-follow response was incorrectly turned into a transport error");
        const auto final_cooldown = guarded.request({"GET", base + "/cooldown-final"});
        check(final_cooldown.status == 200 && guarded_transfer.retry_after == "120",
              "successful response cooldown metadata was lost");
        guarded.set_observer({}, {});
        check(guarded.request({"GET", base + "/redirect-cooldown-positive"}).status == 200,
              "removing polling observers did not restore ordinary redirect behavior");
        const auto connection = result.headers.at("x-connection");
        result = session.request({"HEAD", base + "/echo"});
        check(result.body.empty() && result.headers.at("x-connection") == connection, "HEAD/reuse failure");
        result = session.request({"POST", base + "/echo", {"Authorization: Bearer synthetic", "X-Test: yes"}, "payload"});
        check(result.body == "POST|cookie=yes|payload" && result.headers.at("x-auth") == "yes", "POST transition");
        result = session.request({"GET", base + "/echo"});
        check(result.body == "GET|cookie=yes|" && result.headers.at("x-auth") == "no", "GET transition/header isolation");
        result = session.request({"POST", base + "/redirect", {}, "discard"});
        check(result.body == "GET|cookie=yes|", "native POST redirect method");
        result = session.request({"POST", base + "/redirect", {}, "discard", 1000, false});
        check(result.status == 302 && result.redirect_count == 0 && last_transfer.request_count == 1,
              "registration redirect was followed");
        bool redirect_limited = false;
        try { session.request({"GET", base + "/redirect", {}, {}, 1000, true, 0}); }
        catch (const HttpTransportError& error) {
            redirect_limited = error.curl_code == 47 && !error.proven_pre_dispatch && !last_transfer.request_count;
        }
        check(redirect_limited, "redirect cap did not retain uncertain reservation");
        result = session.request({"GET", base + "/status"});
        check(result.status == 418, "HTTP status incorrectly treated as transport error");
        result = session.request({"GET", base + "/chunked"});
        check(result.body == "abcdef", "chunked body");
        HttpSession isolated(HttpSession::TestOptions{true, {}});
        result = isolated.request({"GET", base + "/echo"});
        check(result.body == "GET||", "cookie session isolation");
        result = session.request({"GET", base + "/crosshost", {"Authorization: Bearer synthetic"}});
        check(result.body == "GET||" && result.headers.at("x-auth") == "no", "cross-host auth/cookie leak");
        const std::string tls_base = argv[2];
        bool untrusted = false;
        try { isolated.request({"GET", tls_base + "/echo"}); }
        catch (const HttpTransportError& error) { untrusted = error.curl_code == 60; }
        check(untrusted, "untrusted TLS certificate accepted");
        HttpSession trusted(HttpSession::TestOptions{true, argv[3]});
        result = trusted.request({"GET", tls_base + "/echo"});
        check(result.status == 200, "test CA trust failed");
        auto mismatch = tls_base;
        mismatch.replace(mismatch.find("localhost"), 9, "127.0.0.1");
        bool hostname_rejected = false;
        try { trusted.request({"GET", mismatch + "/echo"}); }
        catch (const HttpTransportError& error) { hostname_rejected = error.curl_code == 60; }
        check(hostname_rejected, "TLS hostname mismatch accepted");
        bool timed_out = false;
        try { session.request({"GET", base + "/slow", {}, {}, 40}); }
        catch (const HttpTransportError& error) { timed_out = error.curl_code == 28 && !error.proven_pre_dispatch; }
        check(timed_out, "timeout failure");
        bool refused_connection = false;
        try { session.request({"POST", argv[4], {}, "synthetic-payload", 1000, false}); }
        catch (const HttpTransportError& error) {
            refused_connection = error.curl_code == 7 && error.proven_pre_dispatch && !last_transfer.request_count;
        }
        check(refused_connection, "connection refusal was not identified conservatively");
        bool redirected_failure = false;
        try { session.request({"POST", base + "/redirect-unavailable", {}, "synthetic-payload", 1000}); }
        catch (const HttpTransportError& error) {
            redirected_failure = error.curl_code == 7 && !error.proven_pre_dispatch && error.redirect_count == 1;
        }
        check(redirected_failure, "failure after an applied redirect incorrectly marked safe to replay");
        const auto previous_reservations = reservations;
        bool cancelled_before = false;
        try { session.request({"POST", base + "/must-not-dispatch", {}, {}, 1000, false, 10, [] { return true; }}); }
        catch (const HttpTransportError& error) { cancelled_before = error.cancelled; }
        check(cancelled_before && reservations == previous_reservations, "cancellation happened after admission");
        const auto cancel_start = std::chrono::steady_clock::now();
        session.set_cancelled([&] { return std::chrono::steady_clock::now() - cancel_start > std::chrono::milliseconds(40); });
        bool cancelled_during = false;
        try { session.request({"GET", base + "/cancel", {}, {}, 5000}); }
        catch (const HttpTransportError& error) {
            cancelled_during = error.cancelled && !error.proven_pre_dispatch && !last_transfer.request_count;
        }
        session.set_cancelled({});
        check(cancelled_during, "in-flight cancellation was not reported conservatively");
        check(std::chrono::steady_clock::now() - cancel_start < std::chrono::seconds(3), "cancellation was not timely");
        for (const bool partial : {false, true}) {
            bool diagnosed = false;
            try {
                session.request({"POST", base + (partial ? "/apply-partial" : "/apply-no-headers") +
                    "?synthetic-secret-query", {"Authorization: Bearer synthetic-secret-token"},
                    "synthetic-secret-payload", 100});
            } catch (const HttpTransportError& error) {
                diagnosed = true;
                check(error.curl_code == 28, "expected CURLE_OPERATION_TIMEDOUT");
                check(!error.proven_pre_dispatch && !last_transfer.request_count, "unknown POST outcome marked replay safe");
                check(error.elapsed_ms >= 50 && error.elapsed_ms < 2000, "timeout elapsed metadata");
                check(error.http_status == (partial ? 200 : 0), "timeout response stage metadata");
                check(error.received_body_bytes == (partial ? std::string("synthetic-secret-response").size() : 0),
                      "partial response byte metadata");
                const std::string diagnostic = error.what();
                check(diagnostic.find("synthetic-secret") == std::string::npos && diagnostic.find(base) == std::string::npos,
                      "transport diagnostic exposed sensitive data");
                check(diagnostic.find("curl_code=28") != std::string::npos &&
                      diagnostic.find("elapsed_ms=") != std::string::npos &&
                      diagnostic.find("http_status=") != std::string::npos &&
                      diagnostic.find("received_body_bytes=") != std::string::npos, "missing numeric diagnostics");
            }
            check(diagnosed, "accepted request timeout did not produce typed diagnostic");
        }
        bool rate_metadata = false;
        try {
            session.request({"POST", base + "/rate-partial", {}, "synthetic-secret-payload", 100});
        } catch (const HttpTransportError& error) {
            rate_metadata = error.curl_code == 28 && error.http_status == 429 &&
                error.retry_after == "120" && last_transfer.retry_after == "120" &&
                !error.proven_pre_dispatch;
        }
        check(rate_metadata, "truncated 429 Retry-After metadata was lost");
        result = session.request({"POST", base + "/apply-mixed", {}, "synthetic-secret-payload", 1000});
        check(result.status == 200 && result.body == "{\"results\":[{\"success\":true},{\"success\":false}]}",
              "completed mixed application results incorrectly treated as transport failure");
        std::cout << "transport checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
