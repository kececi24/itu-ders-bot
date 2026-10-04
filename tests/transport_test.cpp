#include "src/http.hpp"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        check(argc == 4, "fixture URL missing");
        const std::string base = argv[1];
        HttpSession production;
        bool refused = false;
        try { production.prepare({"GET", base}); } catch (...) { refused = true; }
        check(refused, "production accepted cleartext HTTP");
        HttpSession session(HttpSession::TestOptions{true, {}});
        check(session.percent_encode("a&= +/ğ") == "a%26%3D%20%2B%2F%C4%9F", "encoding mismatch");
        check(HttpSession::resolve_url("https://example.org/a/b?x=1", "../login?q=2") == "https://example.org/login?q=2", "URL resolution");
        session.prepare({"GET", base + "/redirect"});
        auto result = session.perform();
        check(result.status == 200 && result.body == "GET|cookie=yes|", "redirect cookie/body failure");
        check(result.effective_url == base + "/echo", "effective URL failure");
        check(result.headers.count("x-intermediate") == 0 && result.headers.at("x-final") == "yes", "final headers failure");
        const auto connection = result.headers.at("x-connection");
        result = session.request({"HEAD", base + "/echo"});
        check(result.body.empty() && result.headers.at("x-connection") == connection, "HEAD/reuse failure");
        result = session.request({"POST", base + "/echo", {"Authorization: Bearer synthetic", "X-Test: yes"}, "payload"});
        check(result.body == "POST|cookie=yes|payload" && result.headers.at("x-auth") == "yes", "POST transition");
        result = session.request({"GET", base + "/echo"});
        check(result.body == "GET|cookie=yes|" && result.headers.at("x-auth") == "no", "GET transition/header isolation");
        result = session.request({"POST", base + "/redirect", {}, "discard"});
        check(result.body == "GET|cookie=yes|", "native POST redirect method");
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
        try { session.request({"GET", base + "/slow", {}, {}, 40}); } catch (...) { timed_out = true; }
        check(timed_out, "timeout failure");
        for (const bool partial : {false, true}) {
            bool diagnosed = false;
            try {
                session.request({"POST", base + (partial ? "/apply-partial" : "/apply-no-headers") +
                    "?synthetic-secret-query", {"Authorization: Bearer synthetic-secret-token"},
                    "synthetic-secret-payload", 100});
            } catch (const HttpTransportError& error) {
                diagnosed = true;
                check(error.curl_code == 28, "expected CURLE_OPERATION_TIMEDOUT");
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
        result = session.request({"POST", base + "/apply-mixed", {}, "synthetic-secret-payload", 1000});
        check(result.status == 200 && result.body == "{\"results\":[{\"success\":true},{\"success\":false}]}",
              "completed mixed application results incorrectly treated as transport failure");
        std::cout << "transport checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
