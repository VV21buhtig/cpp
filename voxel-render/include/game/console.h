#ifndef CONSOLE_H
#define CONSOLE_H

#include <string>
#include <vector>

// Game: внутриигровая консоль (F1). Шрифт stb_easy_font, рендер в NDC.
// Печать идёт и на экран, и в терминал (см. sink в main).
struct GameConsole {
    bool open = false;
    std::string input;
    std::vector<std::string> lines; // последние N
    void print(const std::string& s); // режет по \n, cap 200
    void onChar(unsigned int cp);     // печатаемые
    void backspace();
    void clearInput() { input.clear(); }

    // собрать вершины (x,y,z=rgb-пиксели y-вниз + color) в out; возвращает байты
    void buildQuads(int fbW, std::vector<unsigned char>& outBytes) const;
};

#endif
