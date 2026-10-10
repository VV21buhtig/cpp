// Общая мелочь ядер: уничтожение (ctx всегда calloc — free достаточно).
#include <stdlib.h>
#include "render_api.h"

void rc_destroy(RenderCore *rc) {
    if (rc) free(rc->ctx);
}
