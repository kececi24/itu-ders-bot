#include "token.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string decode_html(std::string value) {
    // Decode once so an escaped entity is not accidentally interpreted twice.
    static const std::regex entity(R"(&(amp|quot|apos|lt|gt|#[0-9]+|#x[0-9a-fA-F]+);)");
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
            try { cp = std::stoul(code.substr(code[1] == 'x' ? 2 : 1), nullptr, code[1] == 'x' ? 16 : 10); }
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
    if (response.status < 200 || response.status >= 300)
        throw std::runtime_error(std::string("Authentication ") + step + " failed (HTTP " + std::to_string(response.status) + ").");
}

bool login_page(const std::string& html) {
    return html.find("ContentPlaceHolder1$tbPassword") != std::string::npos ||
           html.find("ContentPlaceHolder1_tbPassword") != std::string::npos;
}

std::string jwt_value(std::string jwt) {
    auto nonspace = [](unsigned char c) { return !std::isspace(c); };
    jwt.erase(jwt.begin(), std::find_if(jwt.begin(), jwt.end(), nonspace));
    jwt.erase(std::find_if(jwt.rbegin(), jwt.rend(), nonspace).base(), jwt.end());
    static const std::regex compact_jwt(R"([A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+)");
    if (!std::regex_match(jwt, compact_jwt))
        throw std::runtime_error("Authentication failed: JWT endpoint did not return a valid compact token.");
    return jwt;
}
}

namespace token_detail {
std::string fetch(HttpSession& session, const std::string& obs_base,
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
    if (login_page(authenticated.body)) throw std::runtime_error("Authentication failed: the service returned the login form.");
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
        if (login_page(selected.body)) throw std::runtime_error("Authentication identity selection returned the login form.");
    }
    std::cout << "[Auth] Step 3: Finalizing context and fetching JWT...\n";
    auto dashboard = session.request({"GET", HttpSession::resolve_url(obs_base, "/ogrenci/"), {}, {}});
    require_success(dashboard, "student context");
    if (login_page(dashboard.body)) throw std::runtime_error("Authentication student context returned the login form.");
    auto jwt = session.request({"GET", HttpSession::resolve_url(obs_base, "/ogrenci/auth/jwt"),
        {"X-Requested-With: XMLHttpRequest", "Accept: application/json, text/plain, */*"}, {}});
    require_success(jwt, "JWT request");
    const auto token = jwt_value(jwt.body);
    if (debug) std::cout << "[Debug] Authentication completed; token and session details omitted.\n";
    return "Bearer " + token;
}
}

std::string TokenFetcher::get_bearer_token(const std::string& username, const std::string& password, bool debug) {
    return token_detail::fetch(session_, "https://obs.itu.edu.tr/", username, password, debug);
}
