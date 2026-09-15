#include "token.hpp"
#include <iostream>
#include <vector>
#include <sstream>
#include <iomanip>

// --- INTERNAL HELPERS ---

// Decodes &amp; to & so the URL parameters are valid
std::string decode_html(std::string str) {
    size_t pos;
    while ((pos = str.find("&amp;")) != std::string::npos) str.replace(pos, 5, "&");
    return str;
}

// Separates https://domain.com/path into "domain.com" and "/path"
void parse_components(const std::wstring& url, std::wstring& host, std::wstring& path) {
    size_t start = url.find(L"://");
    if (start == std::wstring::npos) return;
    start += 3;
    size_t end = url.find(L"/", start);
    if (end == std::wstring::npos) {
        host = url.substr(start);
        path = L"/";
    } else {
        host = url.substr(start, end - start);
        path = url.substr(end);
    }
}

// --- CLASS METHODS ---

TokenFetcher::TokenFetcher() {
    hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/144.0.0.0", 
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    hConnect = WinHttpConnect(hSession, L"obs.itu.edu.tr", INTERNET_DEFAULT_HTTPS_PORT, 0);
}

TokenFetcher::~TokenFetcher() {
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
}

std::string TokenFetcher::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << (int)(unsigned char)c;
        }
    }
    return escaped.str();
}

std::string TokenFetcher::extract_value(const std::string& html, const std::string& name) {
    std::string search_str = "id=\"" + name + "\" value=\"";
    size_t start = html.find(search_str);
    if (start == std::string::npos) return "";
    start += search_str.length();
    size_t end = html.find("\"", start);
    return html.substr(start, end - start);
}

std::string TokenFetcher::get_bearer_token(const std::string& username, const std::string& password, const bool _debug = false) {
    std::cout << "[Auth] Step 1: Initializing handshake with obs.itu.edu.tr..." << std::endl;

    // GET Root to trigger redirect chain
    HINTERNET hReq1 = WinHttpOpenRequest(hConnect, L"GET", L"/", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    WinHttpSendRequest(hReq1, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(hReq1, NULL);

    // DEBUG: Print the final login URL with subSessionId
    wchar_t landed_url[2048]; 
    DWORD url_len = sizeof(landed_url); 
    WinHttpQueryOption(hReq1, WINHTTP_OPTION_URL, landed_url, &url_len); 
    if(_debug) std::wcout << L"[Debug] Final Login URL: " << landed_url << std::endl;

    std::wstring auth_host, auth_path;
    parse_components(landed_url, auth_host, auth_path);

    // Read HTML to get ASP tokens
    std::string html;
    DWORD dwSize = 0;
    while (WinHttpQueryDataAvailable(hReq1, &dwSize) && dwSize > 0) {
        std::vector<char> buffer(dwSize);
        DWORD dwDownloaded = 0;
        WinHttpReadData(hReq1, (LPVOID)&buffer[0], dwSize, &dwDownloaded);
        html.append(buffer.data(), dwDownloaded);
    }
    WinHttpCloseHandle(hReq1);

    // Scrape tokens and Form Action
    std::string vs = extract_value(html, "__VIEWSTATE");
    std::string vsg = extract_value(html, "__VIEWSTATEGENERATOR");
    std::string ev = extract_value(html, "__EVENTVALIDATION");
    
    std::string action_url = "/Login.aspx";
    size_t a_start = html.find("action=\"");
    if (a_start != std::string::npos) {
        size_t a_end = html.find("\"", a_start + 8);
        std::string raw = html.substr(a_start + 8, a_end - (a_start + 8));
        if (raw.find("./") == 0) raw = "/" + raw.substr(2);
        action_url = decode_html(raw);
    }

    // POST Credentials to the AUTH server (girisv3)
    std::cout << "[Auth] Step 2: Submitting credentials to " << std::string(auth_host.begin(), auth_host.end()) << "..." << std::endl;
    std::string post_data = "__VIEWSTATE=" + url_encode(vs) +
        "&__VIEWSTATEGENERATOR=" + url_encode(vsg) +
        "&__EVENTVALIDATION=" + url_encode(ev) +
        "&ctl00$ContentPlaceHolder1$tbUserName=" + url_encode(username) +
        "&ctl00$ContentPlaceHolder1$tbPassword=" + url_encode(password) +
        "&ctl00$ContentPlaceHolder1$btnLogin=" + url_encode("Giriş / Login");

    HINTERNET hAuthConn = WinHttpConnect(hSession, auth_host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    std::wstring wAction(action_url.begin(), action_url.end());
    HINTERNET hReq2 = WinHttpOpenRequest(hAuthConn, L"POST", wAction.c_str(), NULL, landed_url, NULL, WINHTTP_FLAG_SECURE);
    
    std::wstring post_h = L"Content-Type: application/x-www-form-urlencoded\r\n";
    WinHttpSendRequest(hReq2, post_h.c_str(), -1L, (LPVOID)post_data.c_str(), post_data.length(), post_data.length(), 0);
    WinHttpReceiveResponse(hReq2, NULL);

    std::string login_res;
    while (WinHttpQueryDataAvailable(hReq2, &dwSize) && dwSize > 0) {
        std::vector<char> buf(dwSize);
        DWORD dwDownloaded = 0;
        WinHttpReadData(hReq2, (LPVOID)&buf[0], dwSize, &dwDownloaded);
        login_res.append(buf.data(), dwDownloaded);
    }
    WinHttpCloseHandle(hReq2);

    /** TODO: Restructure this part */
    // Handle Identity Selection if page appears
    if (login_res.find("SelectIdentity") != std::string::npos) {
        std::cout << "[Auth] Step 3: Selecting Student Identity..." << std::endl;
        size_t id_pos = login_res.find("href=\"/Login.aspx?identityGuid=");
        if (id_pos != std::string::npos) {
            std::string id_path = decode_html(login_res.substr(id_pos + 6, login_res.find("\"", id_pos + 6) - (id_pos + 6)));
            std::wstring wIdPath(id_path.begin(), id_path.end());
            HINTERNET hReq3 = WinHttpOpenRequest(hAuthConn, L"GET", wIdPath.c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
            WinHttpSendRequest(hReq3, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
            WinHttpReceiveResponse(hReq3, NULL);
            WinHttpCloseHandle(hReq3);
        }
    }
    WinHttpCloseHandle(hAuthConn);

    // Land on Student Dashboard and Fetch JWT
    std::cout << "[Auth] Step 3: Finalizing context and fetching JWT..." << std::endl;
    
    // Visit /ogrenci/
    HINTERNET hReq4 = WinHttpOpenRequest(hConnect, L"GET", L"/ogrenci/", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    WinHttpSendRequest(hReq4, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(hReq4, NULL);
    WinHttpCloseHandle(hReq4);

    // Fetch JWT
    HINTERNET hReq5 = WinHttpOpenRequest(hConnect, L"GET", L"/ogrenci/auth/jwt", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    std::wstring jwt_h = L"X-Requested-With: XMLHttpRequest\r\nAccept: application/json, text/plain, */*\r\n";
    WinHttpSendRequest(hReq5, jwt_h.c_str(), -1L, NULL, 0, 0, 0);
    WinHttpReceiveResponse(hReq5, NULL);

    std::string jwt;
    while (WinHttpQueryDataAvailable(hReq5, &dwSize) && dwSize > 0) {
        std::vector<char> buf(dwSize);
        DWORD dwDown = 0;
        WinHttpReadData(hReq5, (LPVOID)&buf[0], dwSize, &dwDown);
        jwt.append(buf.data(), dwDown);
    }
    WinHttpCloseHandle(hReq5);

    if (jwt.find("<!DOCTYPE") != std::string::npos || jwt.length() < 20) {
        std::cout << "[Debug] JWT response body: " << jwt.substr(0, 100) << "..." << std::endl;
        return "ERROR: Login failed. Body is HTML.";
    }

    return "Bearer " + jwt;
}