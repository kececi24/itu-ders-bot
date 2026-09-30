#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <assert.h>
#include <windows.h>
#include <winhttp.h>

#include <include/class_data.hpp>

CourseManager::CourseManager(){
    hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/144.0.0.0", 
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    hConnect = WinHttpConnect(hSession, L"obs.itu.edu.tr", INTERNET_DEFAULT_HTTPS_PORT, 0);
}

CourseManager::~CourseManager(){
    if(hConnect) WinHttpCloseHandle(hConnect);
    if(hSession) WinHttpCloseHandle(hSession);
}

std::vector<std::pair<std::string, std::string>> parse_course_table(const std::string& html){
    std::vector<std::pair<std::string, std::string>> entries;

    // Matches: <td>[CRN]</td> followed by <td><a ...>[COURSE_CODE]</a></td>
    std::regex rowPattern(
        R"(<td>\s*(\d{5})\s*</td>\s*<td>\s*<a[^>]*>\s*([^<]+?)\s*</a>\s*</td>)",
        std::regex::optimize
    );

    auto words_begin = std::sregex_iterator(html.begin(), html.end(), rowPattern);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        entries.push_back({ match[1].str(), match[2].str() });
    }

    return entries;
}

void CourseManager::fetch_course_list(){

    for(auto const& [key, branchCode] : BRANS_KODU_ID){
        std::string html;
        std::wstring branchCodeW(branchCode.begin(), branchCode.end());
        std::wstring path = L"/public/DersProgram/DersProgramSearch?programSeviyeTipiAnahtari=LS&dersBransKoduId=" + branchCodeW;

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);

        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, NULL)) {
                
                DWORD bytesAvailable = 0;
                while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
                    std::vector<char> buffer(bytesAvailable + 1);
                    DWORD bytesRead = 0;
                    if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                        html.append(buffer.data(), bytesRead);
                    }
                }
            }
            WinHttpCloseHandle(hRequest);
        }

        if(html.empty()){
            std::cerr << "Failed to fetch data " << key << std::endl;
        }

        auto courses = parse_course_table(html);

        for(auto const& [crn, code] : courses){
            int id = std::stoi(crn);

            size_t pos = code.find(' ');
            std::string branch, courseCode;
            if(pos != std::string::npos){
                branch = code.substr(0, pos);
                courseCode = code.substr(pos + 1);
            }
            else{
                std::cerr << "Failed to parse course: " << code << std::endl;
            }

            std::pair<std::string, std::string> result{branch, courseCode};
            
            CourseManager::courseList.insert({id, result});
        }
    }
}

CourseState CourseManager::get_course_state(int crn){
    auto it = CourseManager::courseList.find(crn);
    if(it == CourseManager::courseList.end()){
        std::cerr << "Could not find data for CRN " << crn << std::endl;
        return {0};
    }

    auto branch = it->second.first;
    
    auto it2 = BRANS_KODU_ID.find(branch);
    assert(it2 != BRANS_KODU_ID.end());

    auto branchCode = it2->second;
}