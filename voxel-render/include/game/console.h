#ifndef CONSOLE_H
#define CONSOLE_H

#include <string>
#include <vector>

// Game: данные консоли (история). Рендер — Dear ImGui в main.
struct GameConsole {
    bool open = false;
    char inputBuf[256] = {};
    std::vector<std::string> lines; // последние N
    void print(const std::string& s); // режет по \n, cap 200
};

#endif
