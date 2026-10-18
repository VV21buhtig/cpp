// ed_doc: ДОКУМЕНТЫ РЕДАКТОРА — тот интерфейс, который знает ХОСТ.
//
// Хост (командное ядро, лента, оболочка) умеет ровно это: спросить «что
// открыто», «есть ли что откатить», «есть ли выделение», сохранить,
// откатить/повторить, удалить/переименовать выбранное. ЧТО ВНУТРИ документа
// (виджеты интерфейса, сцена, граф) — не его дело: это дело РЕЖИМА, который
// документ завёл. Как в Unity/UE: активное окно = активный документ, и Edit/
// Undo действуют на НЕГО, а не на конкретный тип ассета.
//
// ГРАНИЦА: в этом файле не должно появиться ни ui_widget.h, ни scene.h.
#pragma once

#define ED_DOC_MAX 16

typedef struct EdDoc {
    const char *kind;  // машинный тип: "ui" / "scene" / "graph"
    const char *title; // человеческое имя (для таба документа)
    int (*dirty)(void);       // есть ли несохранённые правки
    int (*save)(void);        // записать на диск -> 1 если записал
    int (*can_undo)(void);    // есть ли что откатывать
    int (*can_redo)(void);
    int (*undo)(void);        // откатить -> 1 если откатил
    int (*redo)(void);
    int (*has_sel)(void);     // есть ли выделение (edit.delete/edit.rename)
    int (*del_sel)(void);     // удалить выбранное
    int (*rename_sel)(void);  // НАЧАТЬ переименование выбранного
} EdDoc;

// Список (а не один слот) — сразу: документные табы (Ш2b) станут ВИДОМ списка,
// а не переделкой интерфейса.
void ed_doc_reset(void);
int ed_doc_add(const EdDoc *d); // -> индекс или -1 (полно/пусто/дубль)
int ed_doc_n(void);
const EdDoc *ed_doc_at(int i);
int ed_doc_find(const char *kind);
void ed_doc_active_set(int i); // -1 = ни одного (гейты закроются)
int ed_doc_active_i(void);
const EdDoc *ed_doc_active(void);

// Одни и те же вопросы — для ЛЮБОГО документа (ядру больше знать нечего).
// Все функции null-безопасны: нет активного документа — 0/0.
int ed_doc_can_undo(void);
int ed_doc_can_redo(void);
int ed_doc_has_sel(void);
int ed_doc_undo(void);
int ed_doc_redo(void);
int ed_doc_del_sel(void);
int ed_doc_rename_sel(void);
int ed_doc_save(void);
int ed_doc_dirty(void);
const char *ed_doc_kind(void);
const char *ed_doc_title(void);
