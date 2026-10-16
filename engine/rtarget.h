#ifndef ENGINE_RTARGET_H
#define ENGINE_RTARGET_H

#ifdef __cplusplus
extern "C" {
#endif

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
    // Явная чистка прохода (W2 тёплого ABI): 1 = цель ждёт чистки.
    // Пишет ТОЛЬКО eng_pass_begin (композитор/одиночка); ядра читают.
    // Никто больше не тыкает (noClear-хаки убиты: была инверсная семантика
    // "не чистить", размазанная по app-коду).
    int passClear;
    float clearR, clearG, clearB, clearA; // цвет чистки от begin
    float clearDepth; // глубина чистки от begin
    // Нативные хендлы цели по бэкендам (только своё читает своё ядро;
    // числа/указатели вместо GPU-типов — comp.h без GPU-зависимостей).
    // VK (создаёт и владеет vk-ядро, смерть — в его shutdown):
    //   nobj = [VKTarget*], где VKTarget = {w,h,img,view,dimg,dview,rpClear,rpLoad,fb}.
    // WebGPU: nobj = [WGTarget*] (см. webgpu_core).
    void *nobj[2];
    long long nint[2]; // запас чисел (резерв)
};

#ifdef __cplusplus
}
#endif
#endif // ENGINE_RTARGET_H
