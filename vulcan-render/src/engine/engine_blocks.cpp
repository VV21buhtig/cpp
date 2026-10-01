#include "engine/blocks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

BlockRegistry gBlocks;

// ---- Мини-DOM JSON (объекты/массивы/строки/числа/bool/null + // и /* */ комментарии) ----
namespace {
struct JVal {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double num = 0;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;
    const JVal* find(const char* k) const {
        if (t != OBJ) return nullptr;
        for (auto& kv : o)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

static const char* skipWS(const char* p, const char* e) {
    while (p < e) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '/' && p + 1 < e && p[1] == '/') { p += 2; while (p < e && *p != '\n') p++; continue; }
        if (*p == '/' && p + 1 < e && p[1] == '*') {
            p += 2;
            while (p + 1 < e && !(p[0] == '*' && p[1] == '/')) p++;
            if (p + 1 < e) p += 2;
            continue;
        }
        break;
    }
    return p;
}

static void appendUTF8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

static const char* parseVal(const char* p, const char* e, JVal& out) {
    p = skipWS(p, e);
    if (p >= e) return nullptr;
    if (*p == '{') {
        out.t = JVal::OBJ;
        p++;
        p = skipWS(p, e);
        if (p < e && *p == '}') return p + 1;
        while (p < e) {
            JVal k;
            p = parseVal(p, e, k);
            if (!p || k.t != JVal::STR) return nullptr;
            p = skipWS(p, e);
            if (p >= e || *p != ':') return nullptr;
            p++;
            JVal v;
            p = parseVal(p, e, v);
            if (!p) return nullptr;
            out.o.emplace_back(std::move(k.s), std::move(v));
            p = skipWS(p, e);
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == '}') return p + 1;
            return nullptr;
        }
        return nullptr;
    }
    if (*p == '[') {
        out.t = JVal::ARR;
        p++;
        p = skipWS(p, e);
        if (p < e && *p == ']') return p + 1;
        while (p < e) {
            JVal v;
            p = parseVal(p, e, v);
            if (!p) return nullptr;
            out.a.push_back(std::move(v));
            p = skipWS(p, e);
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == ']') return p + 1;
            return nullptr;
        }
        return nullptr;
    }
    if (*p == '"') {
        out.t = JVal::STR;
        p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                if (*p == 'n') out.s += '\n';
                else if (*p == 't') out.s += '\t';
                else if (*p == 'r') out.s += '\r';
                else if (*p == 'u' && p + 4 < e) {
                    char hex[5] = {p[1], p[2], p[3], p[4], 0};
                    appendUTF8(out.s, (unsigned)strtoul(hex, nullptr, 16));
                    p += 4;
                } else out.s += *p;
                p++;
            } else out.s += *p++;
        }
        return (p < e) ? p + 1 : nullptr;
    }
    if (!strncmp(p, "true", 4)) { out.t = JVal::BOOL; out.b = true; return p + 4; }
    if (!strncmp(p, "false", 5)) { out.t = JVal::BOOL; out.b = false; return p + 5; }
    if (!strncmp(p, "null", 4)) { out.t = JVal::NUL; return p + 4; }
    char* end = nullptr;
    double v = strtod(p, &end);
    if (end == p) return nullptr;
    out.t = JVal::NUM;
    out.num = v;
    return end;
}

static int getInt(const JVal* o, const char* k, int dflt) {
    if (!o) return dflt;
    const JVal* v = o->find(k);
    return (v && v->t == JVal::NUM) ? (int)v->num : dflt;
}
static bool getBool(const JVal* o, const char* k, bool dflt) {
    if (!o) return dflt;
    const JVal* v = o->find(k);
    return (v && v->t == JVal::BOOL) ? v->b : dflt;
}
static BlockSnd parseSnd(const JVal* o, const char* k, BlockSnd dflt) {
    if (!o) return dflt;
    const JVal* v = o->find(k);
    if (!v || v->t != JVal::STR) return dflt;
    if (v->s == "wood") return BlockSnd::Wood;
    if (v->s == "leaves" || v->s == "grass") return BlockSnd::Leaves;
    if (v->s == "stone") return BlockSnd::Stone;
    if (v->s == "generic") return BlockSnd::Generic;
    return BlockSnd::Dig; // "dig" и всё неизвестное
}
} // namespace

static void setDefault(BlockRegistry& r, unsigned char id, const char* name,
                       int tTop, int tSide, int tBottom,
                       bool solid, bool fluid, bool cutout, unsigned char emit,
                       BlockSnd brk, BlockSnd plc, int step) {
    BlockDef& d = r.defs[id];
    d.id = id;
    snprintf(d.name, sizeof(d.name), "%s", name);
    d.tileTop = tTop; d.tileSide = tSide; d.tileBottom = tBottom;
    d.solid = solid; d.fluid = fluid; d.cutout = cutout; d.emit = emit;
    d.brk = brk; d.plc = plc; d.stepSurf = step;
}

BlockRegistry::BlockRegistry() {
    for (int i = 0; i < 256; i++) { defs[i].id = (unsigned char)i; } // неизвестные: не-solid
    setDefault(*this, B_AIR, "air", 0, 0, 0, false, false, false, 0, BlockSnd::Dig, BlockSnd::Generic, 0);
    setDefault(*this, B_GRASS, "grass", 0, 1, 2, true, false, false, 0, BlockSnd::Dig, BlockSnd::Dig, 0);
    setDefault(*this, B_DIRT, "dirt", 2, 2, 2, true, false, false, 0, BlockSnd::Dig, BlockSnd::Dig, 0);
    setDefault(*this, B_STONE, "stone", 3, 3, 3, true, false, false, 0, BlockSnd::Stone, BlockSnd::Stone, 1);
    setDefault(*this, B_LEAVES, "leaves", 6, 6, 6, true, false, true, 0, BlockSnd::Leaves, BlockSnd::Leaves, 0);
    setDefault(*this, B_LOG, "log", 8, 7, 8, true, false, false, 0, BlockSnd::Wood, BlockSnd::Wood, 0);
    setDefault(*this, B_WATER, "water", 4, 4, 4, false, true, false, 0, BlockSnd::Dig, BlockSnd::Generic, 0);
    setDefault(*this, B_LAVA, "lava", 5, 5, 5, false, true, false, 14, BlockSnd::Dig, BlockSnd::Generic, 0);
    setDefault(*this, B_COAL, "coal_ore", 9, 9, 9, true, false, false, 0, BlockSnd::Stone, BlockSnd::Stone, 1);
    setDefault(*this, B_IRON, "iron_ore", 10, 10, 10, true, false, false, 0, BlockSnd::Stone, BlockSnd::Stone, 1);
    setDefault(*this, B_GOLD, "gold_ore", 11, 11, 11, true, false, false, 0, BlockSnd::Stone, BlockSnd::Stone, 1);
    setDefault(*this, B_DIAMOND, "diamond_ore", 12, 12, 12, true, false, false, 0, BlockSnd::Stone, BlockSnd::Stone, 1);
}

bool BlockRegistry::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { fclose(f); return false; }
    std::string txt((size_t)sz, 0);
    if (fread(&txt[0], 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);
    JVal root;
    const char* end = parseVal(txt.data(), txt.data() + txt.size(), root);
    if (!end || root.t != JVal::OBJ) return false;
    const JVal* arr = root.find("blocks");
    if (!arr || arr->t != JVal::ARR) return false;
    int n = 0;
    for (auto& it : arr->a) {
        if (it.t != JVal::OBJ) continue;
        const JVal* jid = it.find("id");
        if (!jid || jid->t != JVal::NUM) continue;
        int id = (int)jid->num;
        if (id < 0 || id > 255) continue;
        BlockDef d = defs[id]; // unspecified поля наследуют defaults
        d.id = (unsigned char)id;
        const JVal* jn = it.find("name");
        if (jn && jn->t == JVal::STR) snprintf(d.name, sizeof(d.name), "%s", jn->s.c_str());
        const JVal* jt = it.find("tiles");
        if (jt && jt->t == JVal::OBJ) {
            int all = getInt(jt, "all", -1);
            int top = getInt(jt, "top", all >= 0 ? all : d.tileTop);
            int side = getInt(jt, "side", all >= 0 ? all : top);
            int bottom = getInt(jt, "bottom", all >= 0 ? all : side);
            if (top >= 0 && top < 64) d.tileTop = top;
            if (side >= 0 && side < 64) d.tileSide = side;
            if (bottom >= 0 && bottom < 64) d.tileBottom = bottom;
        }
        d.solid = getBool(&it, "solid", d.solid);
        d.fluid = getBool(&it, "fluid", d.fluid);
        d.cutout = getBool(&it, "cutout", d.cutout);
        d.emit = (unsigned char)getInt(&it, "emit", d.emit);
        if (d.emit > 14) d.emit = 14;
        d.brk = parseSnd(&it, "break", d.brk);
        if (d.brk == BlockSnd::Generic) d.brk = BlockSnd::Dig;
        d.plc = parseSnd(&it, "place", d.plc);
        d.stepSurf = getInt(&it, "step", d.stepSurf);
        if (d.stepSurf < 0 || d.stepSurf > 2) d.stepSurf = 0;
        defs[id] = d;
        n++;
    }
    if (n == 0) return false;
    defs[B_AIR].solid = false; // воздух всегда не-solid
    defs[B_AIR].fluid = false;
    loaded = true;
    count = n;
    printf("blocks: loaded %d defs from %s\n", n, path);
    return true;
}
