#ifndef SDF_GPU_H
#define SDF_GPU_H
// Загрузка UBO на GPU. Память тут когерентна (UMA/Vega: CPU и GPU видят одно),
// а там нет (дискретка RTX: нужен flush; WASM/WebGPU: queue.writeBuffer).
// Правило: зеркало правим всегда, дальше ОДНА точка ухода на GPU:
//   coherent   -> memcpy достаточно;
//   non-coherent (native map) -> memcpy + flush (как vulcan vmaFlushAllocation);
//   web        -> queue.writeBuffer (рантайм сам).
// Пока GPU-слоя нет — заглушка с документом намерения, чтобы не забыть.
#include <string.h>
#include "sdf_ubo.h"

typedef enum { SDF_MEM_COHERENT, SDF_MEM_NONCOHERENT } SdfMemKind;

// Копирует зеркало в mapping. Возвращает 1 если нужен flush после.
static inline int sdf_ubo_stage(void *mapping, const SdfUBO *mirror) {
    memcpy(mapping, mirror, sizeof(SdfUBO));
    return 1; // вызывающий делает flush на non-coherent, пропускает на coherent
}
#endif
