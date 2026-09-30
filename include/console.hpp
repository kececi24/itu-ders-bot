#pragma once

#include "platform.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// ConsoleSession at the executable entry point configures native output.
inline void enable_unicode() {}
inline void enable_ansi() {}
inline void show_cursor(bool show) {
    std::cout << (show ? "\033[?25h" : "\033[?25l") << std::flush;
}
namespace terminal {
using itu::platform::read_line;
}

inline int show_menu(const std::string& prompt, const std::vector<std::string>& options) {
    if (options.empty()) throw std::runtime_error("Menu has no options");
    itu::platform::MenuInput input;
    int selected = 0;
    const int total = static_cast<int>(options.size());
    std::cout << prompt << "\n\n";
    while (true) {
        for (int i = 0; i < total; ++i) {
            if (i == selected) std::cout << "  \033[1;36m> " << options[i] << "\033[0m\n";
            else std::cout << "    " << options[i] << "\n";
        }
        std::cout << std::flush;
        switch (input.read()) {
            case itu::platform::MenuKey::up: selected = (selected - 1 + total) % total; break;
            case itu::platform::MenuKey::down: selected = (selected + 1) % total; break;
            case itu::platform::MenuKey::enter: return selected;
            case itu::platform::MenuKey::end: throw std::runtime_error("End of terminal input");
            case itu::platform::MenuKey::other: break;
        }
        std::cout << "\033[" << total << "A";
    }
}
