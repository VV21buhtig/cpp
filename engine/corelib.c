// Реестр ядер: каждое ядро само регистрируется конструктором,
// движок/игра выбирают по имени или по (домен × бэкенд), не зная состав.
// Линкуешь только pix_core — доступны только пиксели. Честная модульность.
#include <stdlib.h>
#include <string.h>
#include "render_api.h"

typedef RenderCore *(*rc_new_fn)(void);

static struct {
    const char *name;
    unsigned domain, backend;
    rc_new_fn fn;
} rc_reg[16];
static int rc_reg_n = 0;

void rc_register(const char *name, unsigned domain, unsigned backend, rc_new_fn fn) {
    if (!name || !fn || rc_reg_n >= 16) return;
    for (int i = 0; i < rc_reg_n; i++)
        if (!strcmp(rc_reg[i].name, name)) return; // уже есть
    rc_reg[rc_reg_n].name = name;
    rc_reg[rc_reg_n].domain = domain;
    rc_reg[rc_reg_n].backend = backend;
    rc_reg[rc_reg_n].fn = fn;
    rc_reg_n++;
}

RenderCore *rc_create(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < rc_reg_n; i++)
        if (!strcmp(rc_reg[i].name, name)) return rc_reg[i].fn();
    return 0;
}

RenderCore *rc_create_by(unsigned domain, unsigned backend) {
    for (int i = 0; i < rc_reg_n; i++)
        if (rc_reg[i].domain == domain && rc_reg[i].backend == backend)
            return rc_reg[i].fn();
    return 0;
}

void rc_destroy(RenderCore *rc) {
    if (rc) free(rc->ctx);
}
