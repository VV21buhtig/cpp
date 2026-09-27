#include "game/console.h"

void GameConsole::print(const std::string& s) {
    size_t a = 0;
    while (true) {
        size_t p = s.find('\n', a);
        std::string one = (p == std::string::npos) ? s.substr(a) : s.substr(a, p - a);
        if (!one.empty() && one.back() == '\r') one.pop_back();
        lines.push_back(one);
        if (lines.size() > 200) lines.erase(lines.begin(), lines.begin() + (lines.size() - 200));
        if (p == std::string::npos) break;
        a = p + 1;
    }
}
