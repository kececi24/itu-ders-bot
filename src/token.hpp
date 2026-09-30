#ifndef TOKEN_HPP
#define TOKEN_HPP

#include "http.hpp"
#include <string>

class TokenFetcher {
    HttpSession session_;
public:
    TokenFetcher() = default;
    std::string get_bearer_token(const std::string& username,
                                 const std::string& password, bool debug = false);
};

#ifdef ITU_ENABLE_TEST_SEAMS
// Internal fixture seam; production callers always use the fixed OBS endpoints.
namespace token_detail {
std::string fetch(HttpSession& session, const std::string& obs_base,
                  const std::string& username, const std::string& password, bool debug);
}
#endif
#endif
