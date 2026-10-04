// Шим под Linux: wcscpy_s нет в glibc (только MSVC, там 2-arg overload).
// Наш код её не зовёт; только FSR2-бэкенд (2-arg форма). Под MSVC не включается.
#pragma once
#include <wchar.h>
#include <string.h>

#ifndef _WIN32
static inline void wcscpy_s(wchar_t* dst, const wchar_t* src) {
    if (!dst || !src) return;
    size_t i = 0;
    for (; src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
