// §14 Ш1c: кольцо строк консоли (см. ui/ui_log.h). Почему не «лог из панели»:
// писателей будет много (редактор сцены, импорт, скрипты), вид один — консоль.
#include "ui/ui_log.h"
#include <stdarg.h>
#include <stdio.h>

static char g_ring[UI_LOG_LINES][UI_LOG_LINE];
static unsigned g_total = 0; // всего добавлено (кольцо его крутит по модулю)

void ui_log_clear(void) {
    g_total = 0;
    g_ring[0][0] = 0;
}
void ui_log_add(const char *fmt, ...) {
    char *dst = g_ring[g_total % (unsigned)UI_LOG_LINES];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, UI_LOG_LINE, fmt, ap); // всегда нуль-терминируем и усекаем
    va_end(ap);
    g_total++;
}
int ui_log_n(void) {
    return (int)(g_total < (unsigned)UI_LOG_LINES ? g_total : (unsigned)UI_LOG_LINES);
}
const char *ui_log_line(int i) {
    int n = ui_log_n();
    if (i < 0 || i >= n) return "";
    unsigned first = g_total - (unsigned)n; // самая старая из живых
    return g_ring[(first + (unsigned)i) % (unsigned)UI_LOG_LINES];
}
unsigned ui_log_total(void) { return g_total; }
