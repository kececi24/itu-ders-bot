#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <windows.h>
#include <winhttp.h>
#include <string>

class TokenFetcher {
private:
    HINTERNET hSession;
    HINTERNET hConnect;

    std::string perform_request(const std::wstring& method, const std::wstring& path, 
                               const std::string& body = "", const std::wstring& headers = L"");

    std::string extract_value(const std::string& html, const std::string& name);
    std::string url_encode(const std::string& value);

public:
    TokenFetcher();
    ~TokenFetcher();
    std::string get_bearer_token(const std::string& username, const std::string& password, const bool _debug);
};

#endif