#include "token.hpp"
#include <include/nlohmann_json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <cstdint>
#include <utility>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string decode_html(std::string value) {
    // Decode once so an escaped entity is not accidentally interpreted twice.
    static const std::regex entity(R"(&(amp|quot|apos|lt|gt|#[0-9]+|#[xX][0-9a-fA-F]+);)");
    std::string result;
    size_t cursor = 0;
    for (std::sregex_iterator it(value.begin(), value.end(), entity), end; it != end; ++it) {
        result.append(value, cursor, static_cast<size_t>(it->position()) - cursor);
        const auto code = (*it)[1].str();
        if (code == "amp") result += '&';
        else if (code == "quot") result += '"';
        else if (code == "apos") result += '\'';
        else if (code == "lt") result += '<';
        else if (code == "gt") result += '>';
        else {
            unsigned long cp = 0;
            const bool hex = (code[1] == 'x' || code[1] == 'X');
            try { cp = std::stoul(code.substr(hex ? 2 : 1), nullptr, hex ? 16 : 10); }
            catch (...) { throw std::runtime_error("Authentication page contains an invalid HTML entity."); }
            if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp == 0)
                throw std::runtime_error("Authentication page contains an invalid HTML entity.");
            if (cp < 0x80) result += static_cast<char>(cp);
            else if (cp < 0x800) { result += static_cast<char>(0xc0 | (cp >> 6)); result += static_cast<char>(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { result += static_cast<char>(0xe0 | (cp >> 12)); result += static_cast<char>(0x80 | ((cp >> 6) & 63)); result += static_cast<char>(0x80 | (cp & 63)); }
            else { result += static_cast<char>(0xf0 | (cp >> 18)); result += static_cast<char>(0x80 | ((cp >> 12) & 63)); result += static_cast<char>(0x80 | ((cp >> 6) & 63)); result += static_cast<char>(0x80 | (cp & 63)); }
        }
        cursor = static_cast<size_t>(it->position() + it->length());
    }
    result.append(value, cursor, std::string::npos);
    return result;
}

using Attributes = std::map<std::string, std::string>;
Attributes attributes(const std::string& tag) {
    static const std::regex attr(R"attr(([A-Za-z_:][A-Za-z0-9_:.$-]*)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))attr");
    Attributes result;
    for (std::sregex_iterator it(tag.begin(), tag.end(), attr), end; it != end; ++it) {
        result[lower((*it)[1].str())] = decode_html((*it)[2].matched ? (*it)[2].str() : ((*it)[3].matched ? (*it)[3].str() : (*it)[4].str()));
    }
    return result;
}

std::string field(const std::string& html, const std::string& name) {
    static const std::regex input(R"(<input\b[^>]*>)", std::regex::icase);
    for (std::sregex_iterator it(html.begin(), html.end(), input), end; it != end; ++it) {
        auto attrs = attributes(it->str());
        if (attrs["id"] == name || attrs["name"] == name) {
            if (!attrs["value"].empty()) return attrs["value"];
            break;
        }
    }
    throw std::runtime_error("Authentication page is missing required field " + name + ".");
}

void require_success(const HttpResponse& response, const char* step) {
    if (response.status < 200 || response.status >= 300) {
        const auto retry = response.headers.find("retry-after");
        throw AuthError(response.status == 401 ? AuthError::Kind::unauthorized : AuthError::Kind::http_failure,
            std::string("Authentication ") + step + " failed (HTTP " + std::to_string(response.status) + ").",
            response.status, retry == response.headers.end() ? std::string{} : retry->second);
    }
}

bool login_page(const std::string& html) {
    return html.find("ContentPlaceHolder1$tbPassword") != std::string::npos ||
           html.find("ContentPlaceHolder1_tbPassword") != std::string::npos;
}

std::string jwt_value(std::string jwt) {
    if (jwt.size() > 16384)
        throw AuthError(AuthError::Kind::invalid_token, "Authentication failed: JWT token is too large.");
    auto nonspace = [](unsigned char c) { return !std::isspace(c); };
    jwt.erase(jwt.begin(), std::find_if(jwt.begin(), jwt.end(), nonspace));
    jwt.erase(std::find_if(jwt.rbegin(), jwt.rend(), nonspace).base(), jwt.end());
    unsigned dots = 0;
    std::size_t segment = 0;
    for (const unsigned char c : jwt) {
        if (c == '.') {
            if (segment == 0 || ++dots > 2)
                throw AuthError(AuthError::Kind::invalid_token, "Authentication failed: JWT endpoint did not return a valid compact token.");
            segment = 0;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '-') ++segment;
        else throw AuthError(AuthError::Kind::invalid_token, "Authentication failed: JWT endpoint did not return a valid compact token.");
    }
    if (dots != 2 || segment == 0)
        throw AuthError(AuthError::Kind::invalid_token, "Authentication failed: JWT endpoint did not return a valid compact token.");
    return jwt;
}
}

namespace token_detail {
std::optional<std::chrono::system_clock::time_point> expiry_hint(const std::string& compact_token) {
    // Do not interpret an invalid, huge or ambiguous claim; missing expiry stays unknown.
    try {
        const auto token = jwt_value(compact_token);
        const auto first = token.find('.') + 1;
        const auto payload = token.substr(first, token.find('.', first) - first);
        if (payload.size() % 4 == 1) return std::nullopt;
        std::string decoded;
        unsigned accumulator = 0, bits = 0;
        for (const unsigned char c : payload) {
            const unsigned value = c >= 'A' && c <= 'Z' ? c - 'A' :
                c >= 'a' && c <= 'z' ? c - 'a' + 26 :
                c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' ? 62 : 63;
            accumulator = (accumulator << 6) | value;
            bits += 6;
            if (bits >= 8) { bits -= 8; decoded += static_cast<char>((accumulator >> bits) & 255); }
            accumulator &= (1u << bits) - 1;
        }
        if (accumulator != 0) return std::nullopt; // Noncanonical unused base64 bits.
        unsigned exp_keys = 0;
        const auto parsed = nlohmann::json::parse(decoded, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
            if (depth > 16) throw std::runtime_error("JWT claim nesting limit");
            if (depth == 1 && event == nlohmann::json::parse_event_t::key && value == "exp") ++exp_keys;
            return true;
        });
        if (!parsed.is_object() || exp_keys != 1 || !parsed.at("exp").is_number_unsigned()) return std::nullopt;
        const auto seconds = parsed.at("exp").get<std::uint64_t>();
        const auto maximum = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::duration::max()).count();
        if (seconds > static_cast<std::uint64_t>(maximum)) return std::nullopt;
        return std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::seconds(seconds)));
    } catch (...) { return std::nullopt; }
}

TokenResult refresh(HttpSession& session, const std::string& obs_base, bool debug) {
    auto jwt = session.request({"GET", HttpSession::resolve_url(obs_base, "/ogrenci/auth/jwt"),
        {"X-Requested-With: XMLHttpRequest", "Accept: application/json, text/plain, */*"}, {}});
    require_success(jwt, "JWT request");
    if (login_page(jwt.body)) throw AuthError(AuthError::Kind::login_required, "Authentication JWT request returned the login form.");
    const auto token = jwt_value(jwt.body);
    if (debug) std::cout << "[Debug] Authentication completed; token and session details omitted.\n";
    return {"Bearer " + token, expiry_hint(token)};
}

TokenResult fetch_token(HttpSession& session, const std::string& obs_base,
                  const std::string& username, const std::string& password, bool debug) {
    if (username.empty() || password.empty()) throw std::runtime_error("Authentication requires a username and password.");
    std::cout << "[Auth] Step 1: Initializing handshake with obs.itu.edu.tr...\n";
    auto login = session.request({"GET", HttpSession::resolve_url(obs_base, "/"), {}, {}});
    require_success(login, "handshake");
    const auto vs = field(login.body, "__VIEWSTATE");
    const auto vsg = field(login.body, "__VIEWSTATEGENERATOR");
    const auto ev = field(login.body, "__EVENTVALIDATION");
    std::string action = "/Login.aspx";
    static const std::regex form(R"(<form\b[^>]*>)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(login.body, match, form)) {
        auto attrs = attributes(match.str());
        if (attrs.count("action")) action = attrs["action"];
    }
    const auto action_url = HttpSession::resolve_url(login.effective_url, action);
    std::string body = "__VIEWSTATE=" + session.percent_encode(vs) +
        "&__VIEWSTATEGENERATOR=" + session.percent_encode(vsg) +
        "&__EVENTVALIDATION=" + session.percent_encode(ev) +
        "&ctl00$ContentPlaceHolder1$tbUserName=" + session.percent_encode(username) +
        "&ctl00$ContentPlaceHolder1$tbPassword=" + session.percent_encode(password) +
        "&ctl00$ContentPlaceHolder1$btnLogin=" + session.percent_encode("Giriş / Login");
    std::cout << "[Auth] Step 2: Submitting credentials...\n";
    auto authenticated = session.request({"POST", action_url,
        {"Content-Type: application/x-www-form-urlencoded", "Referer: " + login.effective_url}, body});
    require_success(authenticated, "login");
    if (login_page(authenticated.body)) throw AuthError(AuthError::Kind::login_required, "Authentication failed: the service returned the login form.");
    if (authenticated.body.find("SelectIdentity") != std::string::npos) {
        std::cout << "[Auth] Step 3: Selecting Student Identity...\n";
        static const std::regex link(R"(<a\b[^>]*>)", std::regex::icase);
        std::string identity;
        for (std::sregex_iterator it(authenticated.body.begin(), authenticated.body.end(), link), end; it != end; ++it) {
            auto attrs = attributes(it->str());
            if (attrs["href"].find("/Login.aspx?identityGuid=") == 0) { identity = attrs["href"]; break; }
        }
        if (identity.empty()) throw std::runtime_error("Authentication identity selection link is missing.");
        auto selected = session.request({"GET", HttpSession::resolve_url(authenticated.effective_url, identity), {}, {}});
        require_success(selected, "identity selection");
        if (login_page(selected.body)) throw AuthError(AuthError::Kind::login_required, "Authentication identity selection returned the login form.");
    }
    std::cout << "[Auth] Step 3: Finalizing context and fetching JWT...\n";
    auto dashboard = session.request({"GET", HttpSession::resolve_url(obs_base, "/ogrenci/"), {}, {}});
    require_success(dashboard, "student context");
    if (login_page(dashboard.body)) throw AuthError(AuthError::Kind::login_required, "Authentication student context returned the login form.");
    return refresh(session, obs_base, debug);
}
#ifdef ITU_ENABLE_TEST_SEAMS
std::string fetch(HttpSession& session, const std::string& obs_base,
                  const std::string& username, const std::string& password, bool debug) {
    return fetch_token(session, obs_base, username, password, debug).bearer;
}
#endif
}

AuthError::AuthError(Kind error_kind, const std::string& message, long http_status, std::string retry)
    : std::runtime_error(message), kind(error_kind), status(http_status), retry_after(std::move(retry)) {}

TokenResult TokenFetcher::get_token(const std::string& username, const std::string& password, bool debug) {
    return token_detail::fetch_token(session_, obs_base_, username, password, debug);
}
TokenResult TokenFetcher::refresh_token(bool debug) { return token_detail::refresh(session_, obs_base_, debug); }
void TokenFetcher::set_observer(HttpSession::BeforeTransfer before, HttpSession::AfterTransfer after) {
    // Only polling installs a governor. Its cooldown must cover every HTTP
    // exchange, including a redirect that libcurl would otherwise follow
    // before the governor can inspect the response.
    const bool guarded = static_cast<bool>(before) || static_cast<bool>(after);
    session_.set_observer(std::move(before), std::move(after), guarded);
}
void TokenFetcher::set_cancelled(std::function<bool()> cancelled) { session_.set_cancelled(std::move(cancelled)); }
#ifdef ITU_ENABLE_TEST_SEAMS
TokenFetcher::TokenFetcher(const HttpSession::TestOptions& options, std::string obs_base)
    : session_(options), obs_base_(std::move(obs_base)) {}
#endif
std::string TokenFetcher::get_bearer_token(const std::string& username, const std::string& password, bool debug) {
    return get_token(username, password, debug).bearer;
}
