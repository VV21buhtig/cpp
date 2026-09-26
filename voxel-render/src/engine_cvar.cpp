#include "engine/cvar.h"
#include <unordered_map>
#include <cstdio>
#include <iostream>
#include <sstream>

static std::unordered_map<std::string, float> g_vars;

void CVarSys::reg(const std::string& name, float def) {
    if (!g_vars.count(name)) g_vars[name] = def;
}

float CVarSys::get(const std::string& name, float fallback) const {
    auto it = g_vars.find(name);
    return it != g_vars.end() ? it->second : fallback;
}

void CVarSys::set(const std::string& name, float v) {
    auto it = g_vars.find(name);
    if (it == g_vars.end()) { std::cout << "unknown cvar: " << name << "\n"; return; }
    it->second = v;
    std::cout << name << " = " << v << "\n";
}

void CVarSys::list() const {
    for (auto& kv : g_vars) std::cout << "  " << kv.first << " = " << kv.second << "\n";
}

bool CVarSys::save(const char* path) const {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "w");
    if (!f) return false;
    for (auto& kv : g_vars) fprintf(f, "%s %.6f\n", kv.first.c_str(), kv.second);
    if (fclose(f) != 0) return false;
    return rename(tmp, path) == 0;
}

bool CVarSys::load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char name[128];
    float v;
    int n = 0;
    while (fscanf(f, "%127s %f", name, &v) == 2) {
        if (g_vars.count(name)) { g_vars[name] = v; n++; }
        else std::cout << "cfg: unknown " << name << "\n";
    }
    fclose(f);
    std::cout << "cfg loaded " << n << " vars\n";
    return true;
}

void CVarSys::exec(const std::string& line) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd == "set") {
        std::string n; float v;
        if (ss >> n >> v) set(n, v);
        else std::cout << "usage: set <name> <value>\n";
    } else if (cmd == "get") {
        std::string n;
        if (ss >> n) std::cout << n << " = " << get(n) << "\n";
        else std::cout << "usage: get <name>\n";
    } else if (cmd == "list") list();
    else if (cmd == "save") {
        std::string p; ss >> p;
        std::cout << (save(p.empty() ? "gfx.cfg" : p.c_str()) ? "saved\n" : "save FAILED\n");
    } else if (cmd == "load") {
        std::string p; ss >> p;
        if (!load(p.empty() ? "gfx.cfg" : p.c_str())) std::cout << "load FAILED\n";
    } else if (cmd == "help" || cmd == "?") {
        std::cout << "set <n> <v> | get <n> | list | save [f] | load [f] | help\n";
    } else if (!cmd.empty() && cmd[0] != '#') {
        std::cout << "unknown: " << cmd << "\n";
    }
}
