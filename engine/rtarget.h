#ifndef ENGINE_RTARGET_H
#define ENGINE_RTARGET_H
// Общая цель кадра: текстура, в которую рисуют несколько ядер по очереди.
// Backend создаёт (сейчас только GL: FBO+RGBA; VK позже).
// Имя struct совпадает с forward в render_api.h (один тип, не два).
// Числа вместо GL-типов, чтобы comp.h остался без GPU-зависимостей.
struct RcTarget {
    unsigned backend; // RC_BACKEND_*
    int w, h;
    unsigned fbo; // GL: FBO; остальным: 0 (не умеют)
    unsigned tex; // GL: RGBA-текстура результата
    unsigned dbo; // GL: глубина мишени (живёт с ней)
};
#endif
