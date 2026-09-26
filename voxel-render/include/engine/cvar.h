#ifndef CVAR_H
#define CVAR_H

#include <string>
#include <functional>

// Engine: консольные переменные. Значения — float, `set/get/list/save/load`.
struct CVarSys {
    // sink экрана: дублирует вывод (терминал идёт всегда)
    std::function<void(const std::string&)> onPrint;
    void say(const std::string& s);
    void reg(const std::string& name, float def);
    float get(const std::string& name, float fallback = 0.0f) const;
    void set(const std::string& name, float v);
    void list();
    bool save(const char* path) const; // атомарно через .tmp
    bool load(const char* path);
    // одна строка консоли: set/get/list/save/load/help
    void exec(const std::string& line);
};

#endif
