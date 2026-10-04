#!/usr/bin/env python3
# Генератор пермутаций FSR2 под наш фиксированный конфиг (замена ffx_sc, которого
# нет под Linux — это .exe; логика 1:1 из src/ffx-fsr2-api/vk/CMakeLists.txt).
# На вход: glslangValidator, dirs, список пассов. На выход: *_permutations.h
# Формат — как ждёт ffx_fsr2_shaders_vk.cpp (AMD, не правим):
#   PermutationKey (union bitfields+index), PermutationInfo[], IndirectionTable[128].
# Варианты: LANCZOS{0,1} x FFX_HALF{0,1} (luma: HALF=0), остальные фиксированы
# под наш контекст: HDR=1, LOWRES=0, JITTERED=0, INVERTED=0, SHARPEN=0.
# WAVE64 — НЕ вариант шейдера (это pNext requiredSubgroupSize у пайплайна).
import os
import re
import subprocess
import sys

BASE_DEFS = [
    "-DFFX_GPU=1",
    "-DFFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0",
    "-DFFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0",
    "-DFFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1",
    "-DFFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0",
    "-DFFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2",
]
# порядок полей ключа = порядок бит index (GCC/Clang/MSVC: LSB-first, совпадаем)
KEY_FIELDS = [
    "FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE",
    "FFX_FSR2_OPTION_HDR_COLOR_INPUT",
    "FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS",
    "FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS",
    "FFX_FSR2_OPTION_INVERTED_DEPTH",
    "FFX_FSR2_OPTION_APPLY_SHARPENING",
    "FFX_HALF",
]
# бит options (FSR2_SHADER_PERMUTATION_*) -> поле ключа (WAVE64 bit6 пропускаем!)
OPT_TO_FIELD = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 7: 6}

PASSES = [
    "ffx_fsr2_tcr_autogen_pass",
    "ffx_fsr2_autogen_reactive_pass",
    "ffx_fsr2_accumulate_pass",
    "ffx_fsr2_compute_luminance_pyramid_pass",
    "ffx_fsr2_depth_clip_pass",
    "ffx_fsr2_lock_pass",
    "ffx_fsr2_reconstruct_previous_depth_pass",
    "ffx_fsr2_rcas_pass",
]

# классификация по именам FFX (надёжнее кодов типов glslang):
# r_* = sampled, rw_* = storage, cb* с binding = UBO (сэмплеры s_* — бэкенда).
def parse_reflection(text):
    sampled, storage, ubo = [], [], []
    for line in text.splitlines():
        m = re.match(r"^(\S+): offset \S+, type \S+, size \S+, index \S+, binding (\S+),", line)
        if not m:
            continue
        name, binding = m.group(1), m.group(2)
        if binding == "-1":
            continue  # члены блоков/типы, не ресурсы
        if re.match(r"^cb\w+$", name):
            ubo.append((name, int(binding)))
        elif name.startswith("r_"):
            sampled.append((name, int(binding)))
        elif name.startswith("rw_"):
            storage.append((name, int(binding)))
    # стабильный порядок: по binding (как у ffx_sc)
    sampled.sort(key=lambda t: t[1])
    storage.sort(key=lambda t: t[1])
    ubo.sort(key=lambda t: t[1])
    return sampled, storage, ubo


def compile_variant(glslang, src, incs, defines, spv_path):
    cmd = [glslang, "-e", "main", "--target-env", "vulkan1.1", "-S", "comp", "-Os",
           "-DFFX_GLSL=1"] + defines + [f"-I{d}" for d in incs] + ["-o", spv_path, src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"COMPILE FAIL {' '.join(defines)}\n{r.stderr[:2000]}")
        sys.exit(1)
    with open(spv_path, "rb") as f:
        return f.read()


def reflect(glslang, src, incs, defines):
    cmd = [glslang, "-l", "-q", "-e", "main", "--target-env", "vulkan1.1",
           "-S", "comp", "-DFFX_GLSL=1"] + defines + [f"-I{d}" for d in incs] + [src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"REFLECT FAIL\n{r.stderr[:2000]}")
        sys.exit(1)
    return parse_reflection(r.stdout)


def c_bytes(data):
    return ",\n".join(
        ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) for i in range(0, len(data), 12))


def str_array(name, items):
    if not items:
        return f"static const char* {name}[] = {{ nullptr }};"
    return f"static const char* {name}[] = {{{', '.join(chr(34) + i + chr(34) for i in items)}}};"

def u32_array(name, items):
    if not items:
        return f"static const uint32_t {name}[] = {{ 0 }};"
    return f"static const uint32_t {name}[] = {{{', '.join(str(i) for i in items)}}};"


def gen_pass(glslang, shaders_dir, incs, tmpdir, outdir, pname, half_opts):
    variants = {}  # (lanczos, half) -> (spv, sampled, storage, ubo)
    for lz in (0, 1):
        for hf in half_opts:
            defines = list(BASE_DEFS) + [
                f"-DFFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE={lz}",
                "-DFFX_FSR2_OPTION_HDR_COLOR_INPUT=1",
                "-DFFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS=0",
                "-DFFX_FSR2_OPTION_JITTERED_MOTION_VECTORS=0",
                "-DFFX_FSR2_OPTION_INVERTED_DEPTH=0",
                "-DFFX_FSR2_OPTION_APPLY_SHARPENING=0",
                f"-DFFX_HALF={hf}",
            ]
            src = os.path.join(shaders_dir, pname + ".glsl")
            spv = os.path.join(tmpdir, f"{pname}_lz{lz}_h{hf}.spv")
            data = compile_variant(glslang, src, incs, defines, spv)
            sampled, storage, ubo = reflect(glslang, src, incs, defines)
            variants[(lz, hf)] = (data, sampled, storage, ubo)
            print(f"  {pname} lz={lz} half={hf}: {len(data)}B srv={len(sampled)} uav={len(storage)} cb={len(ubo)}")

    # таблица: индекс ключа (7 бит) -> вариант; luminance без half (маппим на 0)
    is_luma = ("luminance" in pname)
    info_entries = []
    blob_names = {}
    order = []
    table = []
    for index in range(128):
        lz = (index >> 0) & 1
        hf = (index >> 6) & 1
        if is_luma:
            hf = 0
        key = (lz, hf)
        if key not in blob_names:
            blob_names[key] = len(order)
            order.append(key)
        table.append(blob_names[key])

    lines = []
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append(f"// GENERATED by gen_permutations.py — DO NOT EDIT ({pname})")
    lines.append("typedef struct %s_PermutationKey {" % pname)
    lines.append("    union {")
    lines.append("        struct {")
    for f in KEY_FIELDS:
        lines.append("            uint32_t %s : 1;" % f)
    lines.append("        };")
    lines.append("        uint32_t index;")
    lines.append("    };")
    lines.append("} %s_PermutationKey;" % pname)
    lines.append("typedef struct %s_PermutationInfo {" % pname)
    for t in ["const uint8_t* blobData", "uint32_t blobSize",
              "uint32_t numStorageImageResources", "uint32_t numSampledImageResources",
              "uint32_t numUniformBufferResources",
              "const char** storageImageResourceNames", "const uint32_t* storageImageResourceBindings",
              "const char** sampledImageResourceNames", "const uint32_t* sampledImageResourceBindings",
              "const char** uniformBufferResourceNames", "const uint32_t* uniformBufferResourceBindings"]:
        lines.append("    %s;" % t)
    lines.append("} %s_PermutationInfo;" % pname)
    for vi, key in enumerate(order):
        data, sampled, storage, ubo = variants[key]
        lines.append("static const uint8_t g_%s_blob%d[] = {\n%s\n};" % (pname, vi, c_bytes(data)))
        lines.append(str_array("g_%s_srvN%d" % (pname, vi), [n for n, _ in sampled]))
        lines.append(u32_array("g_%s_srvB%d" % (pname, vi), [b for _, b in sampled]))
        lines.append(str_array("g_%s_uavN%d" % (pname, vi), [n for n, _ in storage]))
        lines.append(u32_array("g_%s_uavB%d" % (pname, vi), [b for _, b in storage]))
        lines.append(str_array("g_%s_cbN%d" % (pname, vi), [n for n, _ in ubo]))
        lines.append(u32_array("g_%s_cbB%d" % (pname, vi), [b for _, b in ubo]))
    lines.append("static const %s_PermutationInfo g_%s_PermutationInfo[] = {" % (pname, pname))
    for vi, key in enumerate(order):
        _, sampled, storage, ubo = variants[key]
        lines.append("    { g_%s_blob%d, sizeof(g_%s_blob%d), %d, %d, %d," % (pname, vi, pname, vi,
                                                                              len(storage), len(sampled), len(ubo)))
        lines.append("      g_%s_uavN%d, g_%s_uavB%d, g_%s_srvN%d, g_%s_srvB%d, g_%s_cbN%d, g_%s_cbB%d }," % ((pname, vi) * 3 + (pname, vi) * 2 + (pname, vi)))
    lines.append("};")
    lines.append("static const int32_t g_%s_IndirectionTable[128] = {%s};" % (pname, ", ".join(str(t) for t in table)))
    with open(os.path.join(outdir, pname + "_permutations.h"), "w") as f:
        f.write("\n".join(lines) + "\n")

    # проверка: ожидаемые флаги рантайма находят реальные блобы
    for lz in (0, 1):
        for hf in (0, 1):
            idx = (lz) | (1 << 1) | ((hf) << 6)  # HDR=1 всегда
            assert 0 <= table[idx] < len(order), (pname, idx)


def main():
    if len(sys.argv) != 6:
        print("usage: gen_permutations.py <glslang> <shaders_dir> <vk_shaders_inc> <tmpdir> <outdir>")
        sys.exit(1)
    glslang, shaders_dir, vk_inc, tmpdir, outdir = sys.argv[1:6]
    os.makedirs(tmpdir, exist_ok=True)
    os.makedirs(outdir, exist_ok=True)
    incs = [shaders_dir, vk_inc]
    for pname in PASSES:
        half_opts = (0,) if "luminance" in pname else (0, 1)
        print(f"pass {pname}:")
        gen_pass(glslang, shaders_dir, incs, tmpdir, outdir, pname, half_opts)
    print("ALL PERMUTATIONS OK")


if __name__ == "__main__":
    main()
