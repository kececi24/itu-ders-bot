#include "src/token.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        HttpSession session(HttpSession::TestOptions{true, ""});
        const auto token = token_detail::fetch(session, argv[1], "test ü&+", "fixture-password\t&+", true);
        if (std::string(argv[2]) != "success" || token != "Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.signature") return 3;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (std::string(argv[2]) != "failure") return 4;
    }
    return 0;
}
