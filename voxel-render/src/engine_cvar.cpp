#include "engine/cvar.h"
#include <unordered_map>
#include <cstdio>
#include <iostream>
#include <sstream>

void CVarSys::say(const std::string& s) {
    std::cout << s;
    if (onPrint) onPrint(s);
}

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
    if (it == g_vars.end()) { say("unknown cvar: " + name + "\n"); return; }
    it->second = v;
    say(name + " = " + std::to_string(v) + "\n");
}

void CVarSys::list() {
    for (auto& kv : g_vars) say("  " + kv.first + " = " + std::to_string(kv.second) + "\n");
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
        else say(std::string("cfg: unknown ") + name + "\n");
    }
    fclose(f);
    say("cfg loaded " + std::to_string(n) + " vars\n");
    return true;
}

void CVarSys::exec(const std::string& line) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd == "set") {
        std::string n; float v;
        if (ss >> n >> v) set(n, v);
        else say("usage: set <name> <value>\n");
    } else if (cmd == "get") {
        std::string n;
        if (ss >> n) say(n + " = " + std::to_string(get(n)) + "\n");
        else say("usage: get <name>\n");
    } else if (cmd == "list") list();
    else if (cmd == "save") {
        std::string p; ss >> p;
        say(save(p.empty() ? "gfx.cfg" : p.c_str()) ? "saved\n" : "save FAILED\n");
    } else if (cmd == "load") {
        std::string p; ss >> p;
        if (!load(p.empty() ? "gfx.cfg" : p.c_str())) say("load FAILED\n");
    } else if (cmd == "help" || cmd == "?") {
        say("set <n> <v> | get <n> | list | save [f] | load [f] | help\n");
    } else if (!cmd.empty() && cmd[0] != '#') {
        say("unknown: " + cmd + "\n");
    }
}
