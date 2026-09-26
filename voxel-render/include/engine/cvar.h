#ifndef CVAR_H
#define CVAR_H

#include <string>

// Engine: консольные переменные. Значения — float, `set/get/list/save/load`.
struct CVarSys {
    void reg(const std::string& name, float def);
    float get(const std::string& name, float fallback = 0.0f) const;
    void set(const std::string& name, float v);
    void list() const;
    bool save(const char* path) const; // атомарно через .tmp
    bool load(const char* path);
    // одна строка консоли: set/get/list/save/load/help
    void exec(const std::string& line);
};

#endif
