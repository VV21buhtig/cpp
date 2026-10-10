# webgpuelectronsdh_engine — воксельный движок C11 + WebGPU (локальный док, не в git)

> Цель: движок уровня Unity/Godot — один C-код, окна на ПК/ноуте/Linux/Windows/Mac,
> через него — игра. Вывод экспериментов: WebGPU — по качеству, Vulkan — быстрее,
> OpenGL — древний. Рендер здесь лучевой (DDA по вокселям), не растровый.

## Где что (ветки)
- `webgpu_sdf_engine` — прототип рендера (SDF → воксели DDA, небо LUT, стриминг). Заморожен как референс.
- `engine` (текущая) — движок: платформа, ввод, настройки, мир. Работа идёт здесь.
- `webgpu_sdh_engine` — старая ветка с опечаткой, не трогать.

## Сборка и запуск (Arch Linux, проверено)
- Натив: `cmake -B build_c -G Ninja && cmake --build build_c`
  - `./build_c/sdf_native_webgpu` — движок (окно 1280x720)
  - `./build_c/vox_test` — тест генерации (срез чанка)
  - `./build_c/vox_stream` — тест стриминга (кольцо/бюджет/эвикт)
  - Алиасы: `runwebgpu`, `runvox` (см. `~/.bashrc`)
- Веб: `source ~/emsdk/emsdk_env.sh`, `emcmake cmake -B build_web`, `cmake --build build_web`
  - выход: `build_web/sdf_native_webgpu.{html,js,wasm}`, раздать и открыть в Chrome
- Тулинг: `emsdk` в `~/emsdk` (emcc 6.0.11), `ninja` из pacman, WebGPU-рантайм тянется
  сам через CMake FetchContent (нужна сеть), stb — системный `/usr/include/stb`.
- Каждая новая shell: `source ~/emsdk/emsdk_env.sh` (иначе нет emcc).

## Структура
- `csrc/wgpu_main.c` — кадр: ввод, камера, UBO, сабмит (`App` + `app_frame`)
- `csrc/vox/` — воксели: `vox_block.h` (типы), `vox_chunk.h` (16x16x64),
  `vox_gen` (холмы по сиду), `vox_world` (стриминг-кольцо 9x9, бюджет),
  `vox_tex` (атлас 16x16x7 из `../voxel-render/texture/tiles`)
- `csrc/sky_lut.*` — печка неба (trans/ms один раз, skyview по допуску)
- `csrc/sdf_*.h` — математика vec3, арена, UBO 64Б, сцена-зеркало, настройки
- `shaders/sdf/` — WGSL по файлам: `00_ubo`, `15_voxel` (DDA), `20_light`,
  `30_sky`, `40_main`; склейка в `cmake/embed_wgsl.cmake` (есть DEPENDS — правится сама)
- `shaders/sky/` — печка атмосферы (порты wc_* 1:1)
- `src/main.ts` + `index.html` — Electron-оболочка настроек (слайдеры → settings.cfg);
  движок подхватывает файл на лету. `node_modules` битый — `npm start` не работает,
  чинить по необходимости (`rm -rf node_modules/electron && npm i`)
- `settings.cfg` — локальный тюнинг (игнор), `goal_limits.txt` — контракт (тоже игнор,
  руками не коммитить, только явный `git add <файл>`, никаких `add .`)

## Управление в движке
- WASD + Space/Shift, мышь (pointer lock), колесо — скорость, ESC — курсор
- `Tab` — меню (гамма/экспозиция/туман/FOV/тени), стрелки + клик
- `N` — виды (цвет/нормали/глубина/цена), `F1` — дебаг-камера (+Ctrl — кем водить),
  `T` — перемотка времени x36 (Shift+T — назад)
- `settings.cfg`: gamma, exposure, fog, fov, shadow

## Договорённости кода
- C11, `-Wall -Wextra` чисто, 0 malloc в кадре (арены/статика), UBO 64Б + `_Static_assert`
- WGSL: без `float` (только `f32`), `select` вместо тернарника на векторах,
  юниформы с выравниванием 16 (naga валидирует строго)
- wgpu-native причуды: `wgpuInstanceProcessEvents` паникует (ждать слипом),
  `GetCompilationInfo` не реализован (ошибки WGSL — через uncaptured-валидацию),
  present modes только `[Mailbox, Fifo]`, `bytesPerRow % 256 == 0`, R8Uint → `RGBA8Unorm`-стиль имён
- Коммиты мелкие, по шагам; пуш в текущую ветку

## Следующее (движок)
Платформенный слой под ПК/ноут/Linux/Windows/Mac → через него игра.
Ближайшее: веб-проверка после воксельных правок, вода/деревья — позже, по порядку.
