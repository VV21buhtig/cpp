# vulcan-render — воксельный Vulkan-рендер (C++17, Vulkan 1.3)

Форк GL-прототипа: мир/мешер/логика свои (`src/engine`), рендер — Vulkan:
поквадратный мешер (1 грань = 1xu32), vertex pulling, compute-cull → indirect,
shadow atlas, HDR + exposure + bloom + Uchimura, SSAO, TAA, RT AO, A2C.
Мир фиксированный 8x8 чанков (128x128x64), сид 1337. 60fps на RADV Renoir.

## Управление

| Клавиша | Действие |
|---|---|
| WASD + мышь / стрелки, Space/C, Shift, ESC | Камера / выход |
| 1 / 2 / 3 | Солнце утро / полдень / вечер |
| F1 | Рентген теневой карты (весь экран) |
| F2 / F3 / F4 | SSAO / TAA / RT AO вкл-выкл |
| F5 | Подсветка «куда светит» (зелёный=на солнце, красный=от) |
| F6 | Карта теней вкл/выкл |

Переменные окружения: `VK_TOD=0.5` (фикс солнца), `VK_WATERDBG=1` (дамп indirect).

## Сборка Linux (Arch, AMD)

```
cd vulcan-render
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DVulkan_INCLUDE_DIR=$PWD/third-party/Vulkan-Headers \
  -DVulkan_LIBRARY=/usr/lib/libvulkan.so
cmake --build build -j$(nproc)
./build/vulcan_render                      # CWD = корень проекта!
./build/vulcan_render --frames 120         # холостой прогон (ноль VALIDATION)
./build/vulcan_render --cam X Y Z YAW PITCH --frames 60 --shot 59  # кадр в shot.tga
```
Нужно системно: `vulkan-loader`, `glfw`, `glm`, `stb`, `glslc` (Vulkan SDK).
Запуск строго из корня (шейдеры `build/shaders/*.spv`, `assets/tiles/`,
`blocks.json`, `structures.cfg` грузятся относительными путями).

## Сборка Windows 10 (MSVC) — см. WINDOWS_MIGRATION.txt в домашней папке

Коротко: VS2022 + CMake + Vulkan SDK + `vcpkg install glfw3 glm` + `stb_image.h`
в `C:\libs\stb\stb\`, затем:
```
cd vulcan-render
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_CXX_FLAGS="/I C:/libs/stb"
cmake --build build --config Release -j
.\build\Release\vulcan_render.exe
```

## Раскладка кода

- `src/main.cpp` (74 строки) — только оркестрация: мир → core → targets → sets → pipes → кадр.
- `src/vk/` — `bootstrap` (окно/instance/device/swapchain), `targets` (картинки/буферы),
  `descriptors` (сеты + compute-пайпы), `pipelines` (графические пайпы), `frame` (камера/пассы).
- `src/engine/` — форк движка: `engine_world` (L-генерация: континенты/горы 3D/реки/
  пещеры с taper/руды-блобы/биомы), `engine_light` (baked свет), `engine_blocks`
  (`blocks.json`), `struct_*` (дубы/сосны/валуны/шахты-walker).
- `src/sky_atmo.cpp` — bake LUT атмосферы (transmittance 256x64 + multiscatter 32x32).
- `shaders/` — `vk_terrain` (pulling), `shadow`, `cull`, `sky` (Bruneton raymarch),
  `lum/adapt`, `tonemap` (Uchimura), `bright/kdown/kup` (bloom), `water`,
  `ssao`, `taa`, `dbg` (рентген).

Правило пункта: билд + `--frames 120` чисто (ноль VALIDATION) + скрин глазами.

## Дорожка эффектов (готово)

SSAO (depth-only 8 тапов, книга Ch10/04) → TAA (Halton-джиттер, closest-depth
репроекция, YCoCg-clip, билинейная история; небо direction-only) → RT AO
(6 лучей x 3 шага по R8-объёму, F4) → MSAA4 + A2C (листва fwidth-sharpened
coverage, книга Ch10/03). Дальше по плану: стриминг, игра, RT на RT-ядрах.

## Референсы (идеи+математика, не код)

- Kaigen/WebCraft (`kaigen-minecraft-opus-5-5-master` рядом): квады 5xu32 + развёртка
  в VS, GPU-каллинг, стрим-план, небо Брюнетона (T/MS LUT), exposure/bloom/TAA/SSAO,
  ветер листвы `wave_leaves`/`wave_plant` (`wc_common.glsl`), Halton-джиттер.
- Luanti (`luanti-stable-5` рядом): свет (param1 night:4|day:4, BFS -1/воксель,
  encode_light), post-DAG (bloom extract/down/up, exposure 1x1, Hable), инвентарь
  (held-модель, shift-move, double-click).
- Книга 3D-Graphics-Rendering-Cookbook 2nd ed.: Ch05 pulling/cull, Ch10/11
  тени (D16, depthBias, PCF3x3)/SSAO/HDR, Ch10/03 MSAA+A2C (coverage через
  `fwidth`, minSampleShading), Uchimura.
