#ifndef TOKEN_HPP
#define TOKEN_HPP

#include "http.hpp"
#include <chrono>
#include <optional>
#include <string>

struct TokenResult {
    std::string bearer;
    // An unverified scheduling hint only; this does not authenticate JWT claims.
    std::optional<std::chrono::system_clock::time_point> expires_at;
};

class AuthError : public std::runtime_error {
public:
    enum class Kind { unauthorized, login_required, http_failure, invalid_token, invalid_response };
    AuthError(Kind kind, const std::string& message, long status = 0, std::string retry_after = {});
    const Kind kind;
    const long status;
    const std::string retry_after;
};

class TokenFetcher {
    HttpSession session_;
    std::string obs_base_ = "https://obs.itu.edu.tr/";
public:
    TokenFetcher() = default;
    TokenResult get_token(const std::string& username, const std::string& password, bool debug = false);
    TokenResult refresh_token(bool debug = false);
    void set_observer(HttpSession::BeforeTransfer before, HttpSession::AfterTransfer after);
    void set_cancelled(std::function<bool()> cancelled);
#ifdef ITU_ENABLE_TEST_SEAMS
    TokenFetcher(const HttpSession::TestOptions& options, std::string obs_base);
#endif
    std::string get_bearer_token(const std::string& username,
                                 const std::string& password, bool debug = false);
};

namespace token_detail {
std::optional<std::chrono::system_clock::time_point> expiry_hint(const std::string& compact_token);
#ifdef ITU_ENABLE_TEST_SEAMS
// Internal fixture seam; production callers always use the fixed OBS endpoints.
std::string fetch(HttpSession& session, const std::string& obs_base,
                  const std::string& username, const std::string& password, bool debug);
#endif
}
#endif
