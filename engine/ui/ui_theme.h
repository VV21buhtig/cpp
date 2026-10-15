// Одна тема (без светло/тёмно). theme.json: 6 ключей × 4 float.
// Перезагрузка по mtime (hot-reload как у шейдеров). Парсер — сканер под
// фиксированный формат (полного JSON нет и не надо: файл наш, формат наш).
#ifndef ENGINE_UI_THEME_H
#define ENGINE_UI_THEME_H

typedef struct {
    float bg[4], bar[4], sel[4], tool[4], txt[4], dim[4];
} UiTheme;

void ui_theme_dark(UiTheme *t); // дефолт (если файла нет)
int ui_theme_load(UiTheme *t, const char *path); // 1 ок (mtime запомнили)
int ui_theme_poll(UiTheme *t, const char *path); // 1 если файл изменился+перезалит

#endif // ENGINE_UI_THEME_H
