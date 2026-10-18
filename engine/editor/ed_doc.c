// ed_doc: реализация списка документов редактора. См. ed_doc.h — здесь НЕТ
// ни виджетов, ни сцены: только слоты, активный документ и вызовы через него.
#include "editor/ed_doc.h"

#include <string.h>

static EdDoc g_doc[ED_DOC_MAX];
static int g_docN = 0;
static int g_docAct = -1;

void ed_doc_reset(void) {
    g_docN = 0;
    g_docAct = -1;
}

int ed_doc_add(const EdDoc *d) {
    if (!d || !d->kind || !d->kind[0]) return -1;
    if (g_docN >= ED_DOC_MAX) return -1;
    for (int i = 0; i < g_docN; i++)
        if (strcmp(g_doc[i].kind, d->kind) == 0) return -1; // дубль типа — отказ
    g_doc[g_docN] = *d;
    return g_docN++;
}

int ed_doc_n(void) { return g_docN; }

const EdDoc *ed_doc_at(int i) { return (i >= 0 && i < g_docN) ? &g_doc[i] : 0; }

int ed_doc_find(const char *kind) {
    if (!kind) return -1;
    for (int i = 0; i < g_docN; i++)
        if (strcmp(g_doc[i].kind, kind) == 0) return i;
    return -1;
}

void ed_doc_active_set(int i) {
    g_docAct = (i >= 0 && i < g_docN) ? i : -1;
}

int ed_doc_active_i(void) { return g_docAct; }

const EdDoc *ed_doc_active(void) { return ed_doc_at(g_docAct); }

// Ниже — ровно то, что нужно хосту. Каждый вызов защищён: не у каждого
// документа есть все умения (например, справка умеет только себя показать).
#define DOC_CALL(fn) ((d && d->fn) ? d->fn() : 0)

int ed_doc_can_undo(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(can_undo); }
int ed_doc_can_redo(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(can_redo); }
int ed_doc_has_sel(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(has_sel); }
int ed_doc_undo(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(undo); }
int ed_doc_redo(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(redo); }
int ed_doc_del_sel(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(del_sel); }
int ed_doc_rename_sel(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(rename_sel); }
int ed_doc_save(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(save); }
int ed_doc_dirty(void) { const EdDoc *d = ed_doc_active(); return DOC_CALL(dirty); }

const char *ed_doc_kind(void) {
    const EdDoc *d = ed_doc_active();
    return (d && d->kind) ? d->kind : "";
}

const char *ed_doc_title(void) {
    const EdDoc *d = ed_doc_active();
    return (d && d->title) ? d->title : "";
}
