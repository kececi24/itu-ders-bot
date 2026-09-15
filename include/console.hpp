#pragma once

#include <iostream>
#include <conio.h>
#include <string>
#include <vector>
#include <windows.h>

void enable_unicode(){
    SetConsoleOutputCP(65001); // Allow unicode characters on console
}

void enable_ansi(){
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

void show_cursor(bool show) {
    std::cout << (show ? "\033[?25h" : "\033[?25l");
}

int show_menu(const std::string& prompt, const std::vector<std::string>& options) {
    int selected = 0;
    const int total = static_cast<int>(options.size());

    // Print initial title
    std::cout << prompt << "\n\n";

    while (true) {
        // Render options with arrow marker and cyan highlight
        for (int i = 0; i < total; ++i) {
            if (i == selected) {
                // \033[1;36m = bold cyan, \033[0m = reset
                std::cout << "  \033[1;36m> " << options[i] << "\033[0m\n";
            } else {
                std::cout << "    " << options[i] << "\n";
            }
        }

        // Wait for keypress
        int key = _getch();

        if (key == 0 || key == 224) { // Extended key prefix (arrow keys)
            switch (_getch()) {
                case 72: // Up Arrow
                    selected = (selected - 1 + total) % total;
                    break;
                case 80: // Down Arrow
                    selected = (selected + 1) % total;
                    break;
            }
        } else if (key == 13) { // Enter key
            break;
        }

        // Move cursor up by the number of menu items to redraw in-place
        std::cout << "\033[" << total << "A";
    }

    return selected;
}