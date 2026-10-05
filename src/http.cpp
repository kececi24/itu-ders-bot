#include "http.hpp"
#include <include/platform.hpp>
#ifdef ITU_ENABLE_TEST_SEAMS
#include "http_socket.hpp"
#endif
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <stdexcept>
#include <utility>

namespace {
struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("HTTP initialization failed");
        const auto* version = curl_version_info(CURLVERSION_NOW);
        bool https = false;
        if (version && version->protocols) {
            for (const char* const* protocol = version->protocols; *protocol; ++protocol)
                if (std::string(*protocol) == "https") https = true;
        }
        if (!version || version->version_num < 0x075500 || !https || !(version->features & CURL_VERSION_SSL)) {
            curl_global_cleanup();
            throw std::runtime_error("libcurl 7.85 or later with HTTPS/TLS support is required");
        }
    }
    ~CurlGlobal() { curl_global_cleanup(); }
};
void initialize() { static CurlGlobal global; }
std::string trim(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}
struct Url {
    CURLU* handle = curl_url();
    Url() { if (!handle) throw std::runtime_error("URL allocation failed"); }
    ~Url() { curl_url_cleanup(handle); }
    std::string get(CURLUPart part) {
        char* value = nullptr;
        if (curl_url_get(handle, part, &value, 0) != CURLUE_OK) throw std::runtime_error("Invalid HTTP URL");
        std::unique_ptr<char, decltype(&curl_free)> owner(value, curl_free);
        return value;
    }
};
}

struct HttpSession::Impl {
    CURL* easy = nullptr;
    curl_slist* header_list = nullptr;
    HttpRequest pending;
    HttpResponse response;
    bool prepared = false;
    bool callback_failed = false;
    bool interrupted = false;
    HttpSession::BeforeTransfer before;
    HttpSession::AfterTransfer after;
    std::function<bool()> cancelled;
    bool loopback_only = false;
    std::string ca_file;
    Impl() {
        initialize();
        easy = curl_easy_init();
        if (!easy) throw std::runtime_error("HTTP session allocation failed");
    }
    ~Impl() { curl_slist_free_all(header_list); curl_easy_cleanup(easy); }
    template<class T> void option(CURLoption name, T value) {
        if (curl_easy_setopt(easy, name, value) != CURLE_OK) throw std::runtime_error("HTTP option setup failed");
    }
    bool is_cancelled() {
        return (pending.cancelled && pending.cancelled()) || (cancelled && cancelled());
    }
    static int progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
        auto& self = *static_cast<Impl*>(context);
        try {
            if (self.is_cancelled()) { self.interrupted = true; return 1; }
            return 0;
        } catch (...) { self.callback_failed = true; return 1; }
    }
    static size_t body(char* data, size_t size, size_t count, void* context) noexcept {
        auto& self = *static_cast<Impl*>(context);
        try { self.response.body.append(data, size * count); return size * count; }
        catch (...) { self.callback_failed = true; return 0; }
    }
    static size_t header(char* data, size_t size, size_t count, void* context) noexcept {
        auto& self = *static_cast<Impl*>(context);
        try {
            std::string line(data, size * count);
            if (line.compare(0, 5, "HTTP/") == 0) {
                self.response.headers.clear(); self.response.body.clear();
            } else {
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    self.response.headers[key] = trim(line.substr(colon + 1));
                }
            }
            return size * count;
        } catch (...) { self.callback_failed = true; return 0; }
    }
};

HttpSession::HttpSession() : impl_(std::make_unique<Impl>()) {}
HttpSession::~HttpSession() = default;
#ifdef ITU_ENABLE_TEST_SEAMS
HttpSession::HttpSession(const TestOptions& options) : HttpSession() {
    impl_->loopback_only = options.allow_loopback_http;
    impl_->ca_file = options.ca_file;
}
#endif
void HttpSession::prepare(const HttpRequest& request) {
    auto& s = *impl_;
    s.prepared = false;
    if (request.method != "GET" && request.method != "HEAD" && request.method != "POST") throw std::runtime_error("Unsupported HTTP method");
    if (request.timeout_ms <= 0) throw std::runtime_error("Invalid HTTP timeout");
    if (request.max_redirects < 0 || request.max_redirects > 10) throw std::runtime_error("Invalid HTTP redirect limit");
    Url url;
    if (curl_url_set(url.handle, CURLUPART_URL, request.url.c_str(), 0) != CURLUE_OK) throw std::runtime_error("Invalid HTTP URL");
    const auto scheme = url.get(CURLUPART_SCHEME);
    if (scheme != "https" && !(s.loopback_only && scheme == "http")) throw std::runtime_error("HTTPS is required");
    char* user = nullptr;
    if (curl_url_get(url.handle, CURLUPART_USER, &user, 0) == CURLUE_OK) { curl_free(user); throw std::runtime_error("URL credentials are not allowed"); }
    s.pending = request;
    // easy_reset retains both the cookie engine and connection cache.
    curl_easy_reset(s.easy);
    curl_slist_free_all(s.header_list); s.header_list = nullptr;
    for (const auto& header : s.pending.headers) {
        if (header.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("Invalid HTTP header");
        auto* next = curl_slist_append(s.header_list, header.c_str());
        if (!next) throw std::runtime_error("HTTP header allocation failed");
        s.header_list = next;
    }
    s.option(CURLOPT_URL, s.pending.url.c_str());
    s.option(CURLOPT_HTTPHEADER, s.header_list);
    s.option(CURLOPT_COOKIEFILE, "");
    s.option(CURLOPT_USERAGENT, itu::platform::user_agent());
    s.option(CURLOPT_FOLLOWLOCATION, s.pending.follow_redirects ? 1L : 0L);
    s.option(CURLOPT_MAXREDIRS, s.pending.max_redirects);
    s.option(CURLOPT_PROTOCOLS_STR, s.loopback_only ? "http,https" : "https");
    s.option(CURLOPT_REDIR_PROTOCOLS_STR, s.loopback_only ? "http,https" : "https");
    s.option(CURLOPT_SSL_VERIFYPEER, 1L);
    s.option(CURLOPT_SSL_VERIFYHOST, 2L);
    s.option(CURLOPT_UNRESTRICTED_AUTH, 0L);
    s.option(CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    s.option(CURLOPT_TIMEOUT_MS, s.pending.timeout_ms);
    s.option(CURLOPT_NOSIGNAL, 1L);
    s.option(CURLOPT_NOPROGRESS, 0L);
    s.option(CURLOPT_XFERINFOFUNCTION, &Impl::progress);
    s.option(CURLOPT_XFERINFODATA, &s);
    s.option(CURLOPT_WRITEFUNCTION, &Impl::body);
    s.option(CURLOPT_WRITEDATA, &s);
    s.option(CURLOPT_HEADERFUNCTION, &Impl::header);
    s.option(CURLOPT_HEADERDATA, &s);
    if (!s.ca_file.empty()) s.option(CURLOPT_CAINFO, s.ca_file.c_str());
#ifdef ITU_ENABLE_TEST_SEAMS
    if (s.loopback_only) {
        s.option(CURLOPT_PROXY, "");
        s.option(CURLOPT_OPENSOCKETFUNCTION, &itu_open_loopback);
#ifdef _WIN32
        if (!s.ca_file.empty()) {
            // The offline CA has no CRL/OCSP service. Schannel may tolerate
            // unavailable revocation data for this test CA; revoked chains,
            // trust and hostname verification still fail normally. This seam
            // never enters production or alters its native revocation policy.
            s.option(CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
        }
#endif
    }
#endif
    if (s.pending.method == "HEAD") s.option(CURLOPT_NOBODY, 1L);
    else if (s.pending.method == "POST") {
        s.option(CURLOPT_POST, 1L);
        s.option(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(s.pending.body.size()));
        s.option(CURLOPT_POSTFIELDS, s.pending.body.c_str());
    } else s.option(CURLOPT_HTTPGET, 1L);
    s.prepared = true;
}
HttpTransportError::HttpTransportError(int code, long long elapsed, long status, std::size_t body_bytes,
                                     bool pre_dispatch, bool interrupted, long redirects)
    : std::runtime_error(std::string("HTTP transport failed: ") + curl_easy_strerror(static_cast<CURLcode>(code)) +
          " [curl_code=" + std::to_string(code) + ", elapsed_ms=" + std::to_string(elapsed) +
          ", http_status=" + std::to_string(status) + ", received_body_bytes=" + std::to_string(body_bytes) + "]"),
      curl_code(code), elapsed_ms(elapsed), http_status(status), received_body_bytes(body_bytes),
      proven_pre_dispatch(pre_dispatch), cancelled(interrupted), redirect_count(redirects) {}

HttpResponse HttpSession::perform() {
    auto& s = *impl_;
    if (!s.prepared) throw std::runtime_error("No HTTP request prepared");
    s.prepared = false;
    s.response = {}; s.callback_failed = false; s.interrupted = false;
    if (s.is_cancelled()) throw HttpTransportError(CURLE_ABORTED_BY_CALLBACK, 0, 0, 0, false, true);
    // Admission runs after preparation but before any network operation.
    if (s.before) s.before(s.pending);
    const auto result = curl_easy_perform(s.easy);
    // Transfer information remains available after failures, including a partial response.
    const auto status_result = curl_easy_getinfo(s.easy, CURLINFO_RESPONSE_CODE, &s.response.status);
    const auto redirects_result = curl_easy_getinfo(s.easy, CURLINFO_REDIRECT_COUNT, &s.response.redirect_count);
    long request_bytes = 0;
    const auto bytes_result = curl_easy_getinfo(s.easy, CURLINFO_REQUEST_SIZE, &request_bytes);
    HttpTransferInfo info;
    info.curl_code = static_cast<int>(result);
    info.http_status = status_result == CURLE_OK ? s.response.status : 0;
    info.redirect_count = redirects_result == CURLE_OK ? s.response.redirect_count : 0;
    info.cancelled = s.interrupted;
    // A timeout, empty response or zero upload alone never proves replay safety.
    info.proven_pre_dispatch = (result == CURLE_COULDNT_RESOLVE_PROXY ||
        result == CURLE_COULDNT_RESOLVE_HOST || result == CURLE_COULDNT_CONNECT) &&
        bytes_result == CURLE_OK && request_bytes == 0 && redirects_result == CURLE_OK &&
        s.response.redirect_count == 0 && status_result == CURLE_OK && s.response.status == 0;
    if (result == CURLE_OK && redirects_result == CURLE_OK)
        info.request_count = static_cast<std::size_t>(s.response.redirect_count) + 1;
    if (s.after) s.after(info);
    if (result != CURLE_OK) {
        curl_off_t elapsed_us = 0;
        if (curl_easy_getinfo(s.easy, CURLINFO_TOTAL_TIME_T, &elapsed_us) != CURLE_OK) elapsed_us = 0;
        // Never include URLs, headers, response bodies, or curl's server-derived error buffer.
        throw HttpTransportError(static_cast<int>(result), static_cast<long long>(elapsed_us / 1000),
                                 info.http_status, s.response.body.size(), info.proven_pre_dispatch,
                                 info.cancelled, info.redirect_count);
    }
    char* effective = nullptr;
    if (status_result != CURLE_OK ||
        curl_easy_getinfo(s.easy, CURLINFO_EFFECTIVE_URL, &effective) != CURLE_OK) throw std::runtime_error("HTTP result inspection failed");
    if (effective) s.response.effective_url = effective;
    return std::move(s.response);
}
HttpResponse HttpSession::request(const HttpRequest& request) { prepare(request); return perform(); }
void HttpSession::set_observer(BeforeTransfer before, AfterTransfer after) {
    impl_->before = std::move(before); impl_->after = std::move(after);
}
void HttpSession::set_cancelled(std::function<bool()> cancelled) { impl_->cancelled = std::move(cancelled); }
std::string HttpSession::percent_encode(const std::string& value) {
    if (value.size() > INT_MAX) throw std::runtime_error("Form value too large");
    char* encoded = curl_easy_escape(impl_->easy, value.data(), static_cast<int>(value.size()));
    if (!encoded) throw std::runtime_error("Form encoding failed");
    std::unique_ptr<char, decltype(&curl_free)> owner(encoded, curl_free);
    return encoded;
}
std::string HttpSession::resolve_url(const std::string& base, const std::string& relative) {
    initialize(); Url url;
    if (curl_url_set(url.handle, CURLUPART_URL, base.c_str(), 0) != CURLUE_OK ||
        (!relative.empty() && curl_url_set(url.handle, CURLUPART_URL, relative.c_str(), 0) != CURLUE_OK)) throw std::runtime_error("Invalid form action URL");
    return url.get(CURLUPART_URL);
}
