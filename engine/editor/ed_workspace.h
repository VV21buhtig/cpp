// ed_workspace: РЕЖИМ РАБОТЫ редактора как ДАННЫЕ (а не номера по файлу).
//
// Что такое режим (workspace): (1) какие ЛЕНТЫ (вкладки) у него есть, (2) какие
// ПАНЕЛИ-инструменты он показывает — с их подписью, доком и открытостью по
// умолчанию, (3) его документ (см. ed_doc.h). Центр окна — тоже документ.
//
// ЗАЧЕМ (тот же принцип, что у ed_cmd): было «3 вкладки и 4 панели, номера и
// подписи расползались копиями по ui_edit.c — пятая панель = охота за всеми
// четвёрками». Стало: одна ЗАПИСЬ режима. Пятая панель — одна строка таблицы,
// циклы (доки, порядок, персист, шапки) читают её через функции ниже.
//
// ГРАНИЦА (как у ed_cmd/ed_doc): здесь НЕТ ни виджетов, ни сцены, ни GL. Это
// контейнер записей режимов + доступ. Режим («UI», дальше «СЦЕНА») ЗАВОДИТ
// приложение-режим, а оболочка (ed_shell) ЧИТАЕТ: сколько панелей, как зовутся,
// куда встают, открыта ли по умолчанию.
#pragma once

#define ED_TABS_MAX 8  // ёмкость: лент у режима (сейчас 3: HOME/MODEL/VIEW)
#define ED_PANEL_MAX 8 // ёмкость: инструментов у режима (сейчас 4)
#define ED_WS_MAX 8    // ёмкость: режимов у редактора (сейчас 1: "ui")

// Док панели: куда она встаёт в окне. Одна точка правды (не номера по файлу).
enum { ED_DK_LEFT = 0, ED_DK_RIGHT = 1, ED_DK_BOTTOM = 2 };

// Одна панель-инструмент режима.
typedef struct EdPanelDesc {
    const char *id;      // машинный id ("palette"/"tree"/"inspector"/"console")
    const char *caption; // подпись в шапке панели ("PALETTE")
    const char *tag;     // имя ВИДЖЕТА панели в хром-дереве ("EdPalette")
    unsigned char dock;  // ED_DK_*
    unsigned char open;  // открыта по умолчанию (1/0)
    unsigned char look;  // вид панели: 0 = основной фон, 1 = вторичный
} EdPanelDesc;

// Один режим работы редактора.
typedef struct EdWorkspace {
    const char *id;    // машинный id ("ui" / "scene")
    const char *title; // человеческая метка ("UI" / "SCENE")
    const char *tab[ED_TABS_MAX]; // ленты режима
    int tabN;
    EdPanelDesc panel[ED_PANEL_MAX]; // инструменты режима (порядок = порядок в коде)
    int panelN;
} EdWorkspace;

void ed_ws_reset(void);              // очистить список (тесты/перезапуск)
int ed_ws_add(const EdWorkspace *w); // -> индекс записи или -1 (полно/пустой id)
int ed_ws_n(void);                   // сколько режимов заведено
const EdWorkspace *ed_ws_at(int i);  // запись по индексу (0 = нет)
int ed_ws_find(const char *id);      // -> индекс или -1
void ed_ws_active_set(int i);        // какой режим активен (границы проверяются)
int ed_ws_active_i(void);            // индекс активного (-1 = нет)
const EdWorkspace *ed_ws_active(void);

// Что читает оболочка и режим (ВСЕГДА через эти функции, не через таблицу):
int ed_ws_tabs(void);                // сколько лент у активного режима
const char *ed_ws_tab(int i);        // подпись ленты ("" вне границ)
int ed_ws_panels(void);              // сколько инструментов у активного режима
const EdPanelDesc *ed_ws_panel(int i); // запись панели (0 вне границ)
const char *ed_ws_caption(int i);    // подпись панели ("" вне границ)
const char *ed_ws_tag(int i);        // имя виджета панели в хром-дереве ("" вне границ)
int ed_ws_dock(int i);               // док панели (ED_DK_LEFT вне границ)
int ed_ws_open_default(int i);       // открыта ли панель по умолчанию (0 вне границ)
int ed_ws_look(int i);               // вид панели (1 = вторичный фон вне границ)
