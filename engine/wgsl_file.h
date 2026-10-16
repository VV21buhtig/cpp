// WGSL из файлов (Этап 3.1). Malloc-строки: free после make_module.
#ifndef ENGINE_WGSL_FILE_H
#define ENGINE_WGSL_FILE_H

// shaders/sdf/*.wgsl по именам, часть + "\n" (байт-в-байт как embed).
// NULL = нет каталога/файлов.
char *wgsl_sdf(void);
// Небо: which TRANS/MS/VIEW/AMB = 00_atmo + "\n" + модуль.
char *wgsl_sky(const char *which);

#endif // ENGINE_WGSL_FILE_H
