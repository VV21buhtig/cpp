// editor_main: ВХОД ХОСТА — приложения-редактора.
//
// Хозяин — редактор: он знает СПИСОК режимов (ed_mode) и выбирает режим по id,
// а не по номеру в коде. Сам режим ведёт окно, кадр и свои документы. Так «правка
// фронта» (ui) — РЕЖИМ редактора, а не отдельная программа с редактором внутри
// (исправление 18.10 в плане).
//
// Здесь НЕТ ни виджетов, ни GL, ни сцены: вход только выбирает режим (граница как
// у ed_mode/ed_cmd).
//
// Запуск: editor [--mode ui] [ключи режима]   (по умолчанию — "ui")
#include <stdio.h>
#include <string.h>

#include "editor/ed_mode.h"
#include "editor/mode_ui.h"

int main(int argc, char **argv) {
    ed_mode_add(&ED_MODE_UI); // какие режимы есть — решает вход, а не каждый файл
    const char *id = "ui";    // режим по умолчанию
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) id = argv[++i];
    const EdMode *m = ed_mode_find(id);
    if (!m) { // печатаем список: иначе непонятно, что вообще есть
        printf("editor: нет режима '%s'. Режимы:", id);
        for (int i = 0; i < ed_mode_n(); i++) printf(" %s", ed_mode_at(i)->id);
        printf("\n");
        return 1;
    }
    return m->run(argc, argv); // дальше — дело режима (окно, кадр, документы)
}
