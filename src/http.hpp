#ifndef ITU_HTTP_HPP
#define ITU_HTTP_HPP

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long timeout_ms = 30000;
};
struct HttpResponse {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string effective_url;
};

// Diagnostics describe the observed transfer, not whether the server applied a request.
class HttpTransportError : public std::runtime_error {
public:
    HttpTransportError(int code, long long elapsed, long status, std::size_t body_bytes);
    const int curl_code;
    const long long elapsed_ms;
    const long http_status;
    const std::size_t received_body_bytes;
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
    std::string percent_encode(const std::string& value);
    static std::string resolve_url(const std::string& base, const std::string& relative);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif
