#include "game/console.h"
#define STB_EASY_FONT_IMPLEMENTATION
#include <stb/stb_easy_font.h>
#include <cstring>

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

void GameConsole::onChar(unsigned int cp) {
    if (cp >= 32 && cp < 127 && input.size() < 200) input += (char)cp;
}

void GameConsole::backspace() {
    if (!input.empty()) input.pop_back();
}

struct EV {
    float x, y, z;
    unsigned char c[4];
};

static void emitText(const std::string& s, float ox, float oy, const unsigned char col[4],
                     std::vector<EV>& tris) {
    static char buf[1 << 20];
    int nq = stb_easy_font_print(ox / 2, oy / 2, (char*)s.c_str(), (unsigned char*)col, buf, sizeof(buf));
    EV* v = (EV*)buf;
    for (int q = 0; q < nq; q++) {
        EV quad[4];
        for (int k = 0; k < 4; k++) {
            quad[k] = v[q * 4 + k];
            quad[k].x = ox + (quad[k].x - ox / 2) * 2.0f; // масштаб x2 от начала строки
            quad[k].y = oy + (quad[k].y - oy / 2) * 2.0f;
        }
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) tris.push_back(quad[idx[k]]);
    }
}

void GameConsole::buildQuads(int fbW, std::vector<unsigned char>& outBytes) const {
    (void)fbW;
    std::vector<EV> tris;
    const int LH = 24, X0 = 8;
    size_t n = lines.size() > 18 ? 18 : lines.size();
    size_t first = lines.size() - n;
    int rows = (int)n + 1;
    int hgt = rows * LH + 16;
    // фон
    EV bg[6];
    float x0 = 0, y0 = 0, x1 = 100000, y1 = (float)hgt;
    unsigned char bc[4] = {0, 0, 0, 160};
    EV corners[4] = {{x0,y0,0,{bc[0],bc[1],bc[2],bc[3]}},{x1,y0,0,{bc[0],bc[1],bc[2],bc[3]}},
                     {x1,y1,0,{bc[0],bc[1],bc[2],bc[3]}},{x0,y1,0,{bc[0],bc[1],bc[2],bc[3]}}};
    const int bi[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) tris.push_back(corners[bi[k]]);
    (void)bg;
    unsigned char wc[4] = {220, 220, 220, 255};
    for (size_t i = 0; i < n; i++)
        emitText(lines[first + i], (float)X0, (float)(8 + i * LH), wc, tris);
    unsigned char pc[4] = {120, 255, 120, 255};
    emitText("> " + input + "_", (float)X0, (float)(8 + n * LH), pc, tris);
    outBytes.resize(tris.size() * sizeof(EV));
    memcpy(outBytes.data(), tris.data(), outBytes.size());
}
