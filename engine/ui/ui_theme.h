// Одна тема (без светло/тёмно). theme.json: 6 ключей × 4 float.
// Перезагрузка по mtime (hot-reload как у шейдеров). Парсер — сканер под
// фиксированный формат (полного JSON нет и не надо: файл наш, формат наш).
#ifndef ENGINE_UI_THEME_H
#define ENGINE_UI_THEME_H

typedef struct {
    float bg[4], bar[4], sel[4], tool[4], txt[4], dim[4];
    float round; // px скругления панелей (§12, 0 = остро как раньше)
    float pad; // px отступ строк (§12, 0 = legacy-раскладка)
} UiTheme;

void ui_theme_dark(UiTheme *t); // дефолт (если файла нет)
int ui_theme_load(UiTheme *t, const char *path); // 1 ок (mtime запомнили)
int ui_theme_poll(UiTheme *t, const char *path); // 1 если файл изменился+перезалит
int ui_theme_save(const UiTheme *t, const char *path); // весь JSON (§12)

#endif // ENGINE_UI_THEME_H
