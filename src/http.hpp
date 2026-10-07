#ifndef ITU_HTTP_HPP
#define ITU_HTTP_HPP

#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long timeout_ms = 30000;
    bool follow_redirects = true;
    long max_redirects = 10;
    std::function<bool()> cancelled;

    HttpRequest() = default;
    HttpRequest(std::string m, std::string u, std::vector<std::string> h = {},
                std::string b = {}, long to = 30000, bool fr = true,
                long mr = 10, std::function<bool()> c = {})
        : method(std::move(m)), url(std::move(u)), headers(std::move(h)),
          body(std::move(b)), timeout_ms(to), follow_redirects(fr),
          max_redirects(mr), cancelled(std::move(c)) {}
};
struct HttpResponse {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string effective_url;
    long redirect_count = 0;
};

// Safe numeric accounting only. Unknown usage keeps the governor's reservation.
struct HttpTransferInfo {
    int curl_code = 0;
    long http_status = 0;
    long redirect_count = 0;
    std::optional<std::size_t> request_count;
    bool proven_pre_dispatch = false;
    bool cancelled = false;
    // Bounded Retry-After metadata from the final or deliberately stopped
    // redirect response, used for scheduler
    // cooldown decisions; it is never included in diagnostics.
    std::string retry_after;
};

// Diagnostics describe the observed transfer, not whether the server applied a request.
class HttpTransportError : public std::runtime_error {
public:
    HttpTransportError(int code, long long elapsed, long status, std::size_t body_bytes,
                       bool pre_dispatch = false, bool interrupted = false, long redirects = 0,
                       std::string retry_after = {});
    const int curl_code;
    const long long elapsed_ms;
    const long http_status;
    const std::size_t received_body_bytes;
    const bool proven_pre_dispatch;
    const bool cancelled;
    const long redirect_count;
    const std::string retry_after;
};

// One session owns its connection cache and memory-only cookie store. Not thread-safe.
class HttpSession {
public:
    HttpSession();
    ~HttpSession();
    HttpSession(const HttpSession&) = delete;
    HttpSession& operator=(const HttpSession&) = delete;
#ifdef ITU_ENABLE_TEST_SEAMS
    struct TestOptions { bool allow_loopback_http = false; std::string ca_file; };
    explicit HttpSession(const TestOptions& options);
#endif
    void prepare(const HttpRequest& request);
    HttpResponse perform();
    HttpResponse request(const HttpRequest& request);
    using BeforeTransfer = std::function<void(const HttpRequest&)>;
    using AfterTransfer = std::function<void(const HttpTransferInfo&)>;
    // Polling authentication stops before following a redirect with an active
    // Retry-After. Ordinary sessions retain libcurl's normal redirect behavior.
    void set_observer(BeforeTransfer before, AfterTransfer after,
                      bool stop_on_redirect_cooldown = false);
    void set_cancelled(std::function<bool()> cancelled);
    std::string percent_encode(const std::string& value);
    static std::string resolve_url(const std::string& base, const std::string& relative);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif
