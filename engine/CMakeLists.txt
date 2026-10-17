cmake_minimum_required(VERSION 3.20)
project(voxengine LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SQLite3 REQUIRED)
find_library(VULKAN_LIB vulkan REQUIRED)
include(FetchContent)
FetchContent_Declare(webgpu-distribution
  GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
  GIT_TAG dawn-7069)
FetchContent_MakeAvailable(webgpu-distribution)

# База инструмента: окно+ввод+цикл, слепая к контенту. Ядра линкует игра.
add_executable(engine_test engine_test.c)
# render_api.h тянет sdf_math.h/vox_chunk.h из игры (математика переедет
# в engine/ вместе с игровым кодом — сейчас только путь, не копия).
# render_api.h самодостаточен (vec.h + fwd VoxChunk): путей в игру нет.
target_include_directories(engine_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_test PRIVATE engine_shared)
target_compile_options(engine_test PRIVATE -Wall -Wextra)
# WGSL-канон читается из файлов в рантайме (Этап 3.1, baked sdf_wgsl.h убит).
# SPIR-V для vk-ядра из наших shaders/vk (glslc).
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/spv_vk)
foreach(SH vk_mesh.vert vk_mesh.frag vk_sky.vert vk_sky.frag)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/spv_vk/${SH}.spv
    COMMAND glslc -O -o ${CMAKE_CURRENT_BINARY_DIR}/spv_vk/${SH}.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vk/${SH}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vk/${SH}
    COMMENT "glslc ${SH}")
  list(APPEND VK_SPV ${CMAKE_CURRENT_BINARY_DIR}/spv_vk/${SH}.spv)
endforeach()
# libengine.so: ВЕСЬ движок одной shared-библиотекой (встраивание редактора).
# Демо и тесты линкуют её, а не объекты напрямую — доказываем ship-артефакт.
add_library(engine_shared SHARED
  corelib.c engine.c comp.c comp_target.c
  rhi_gl.c rhi_vk.c rhi_webgpu.c
  vec_core.c pix_core.c sdf_core.c mesh_core.c gl_core.c vk_core.c webgpu_core.c
  vox/vox_gen.c vox/vox_mesh.c vox/vox_tex.c vox_feed.c
  sky_lut.c asset.c scene.c project.c view.c vfs.c third-party/glad/gl.c
  ui/ui_core.c
  ui/ui_text.c
  ui/ui_panels.c
  ui/ui_theme.c
  ui/ui_undo.c
  ui/ui_gizmo.c
  ui/ui_widget.c
  ui/ui_widget_runtime.c
  wgsl_file.c
  project_db.c
  graph.c
  ${VK_SPV})
set_target_properties(engine_shared PROPERTIES
  OUTPUT_NAME engine VERSION 0.2.0 SOVERSION 0)
target_include_directories(engine_shared PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/Vulkan-Headers)
target_compile_definitions(engine_shared PRIVATE
  VK_SPV_DIR="${CMAKE_CURRENT_BINARY_DIR}/spv_vk")
target_link_libraries(engine_shared PUBLIC glfw OpenGL::GL ${VULKAN_LIB} webgpu m)
target_link_libraries(engine_shared PRIVATE SQLite3::SQLite3)
target_compile_options(engine_shared PRIVATE -Wall -Wextra)

# Тест математики движка (переехал из игры).
add_executable(mat4_test mat4_test.c)
target_include_directories(mat4_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mat4_test PRIVATE engine_shared)
target_compile_options(mat4_test PRIVATE -Wall -Wextra)
# Тест композитора L3: порядок + отказы, без GPU.
add_executable(comp_test comp_test.c)
target_include_directories(comp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comp_test PRIVATE engine_shared)
target_compile_options(comp_test PRIVATE -Wall -Wextra)
# Тест RHI: GL-контекст + VK-девайс, без контента.
add_executable(rhi_test rhi_test.c)
target_include_directories(rhi_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rhi_test PRIVATE engine_shared)
target_compile_options(rhi_test PRIVATE -Wall -Wextra)
# Песочница Noita-like: pix-ядро + клеточные частицы (первая L4-физика).
add_executable(sand_demo ../template/sand_demo.c)
target_include_directories(sand_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(sand_demo PRIVATE engine_shared)
target_compile_options(sand_demo PRIVATE -Wall -Wextra)

# Тест L4: гравитация + часы фиксированного шага, чистый CPU.
add_executable(phys_test phys_test.c)
target_include_directories(phys_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phys_test PRIVATE engine_shared)
target_compile_options(phys_test PRIVATE -Wall -Wextra)
# Lua-демо: геймплей на скриптах (vec.lua), рендер в движке.
add_executable(lua_demo ../template/lua_demo.c)
target_include_directories(lua_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(lua_demo PRIVATE engine_shared lua)
target_compile_options(lua_demo PRIVATE -Wall -Wextra)
# Общая 3D-сцена: одна на всех (террейн + SDF-витрина + куб).
# Линкует все 3D-ядра: выбор через --core.
add_executable(scene3d ../template/scene3d.c)
target_compile_definitions(scene3d PRIVATE
  VK_SPV_DIR="${CMAKE_CURRENT_BINARY_DIR}/spv_vk")
target_include_directories(scene3d PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scene3d PRIVATE engine_shared)
target_compile_options(scene3d PRIVATE -Wall -Wextra)
# Общая 2D-сцена: фигуры для vec-ядра.
add_executable(scene2d ../template/scene2d.c)
target_include_directories(scene2d PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(scene2d PRIVATE engine_shared)
target_compile_options(scene2d PRIVATE -Wall -Wextra)
# Композитное демо L3: воксели + векторный UI в одном кадре (общая цель).
add_executable(comp_demo ../template/comp_demo.c)
target_include_directories(comp_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(comp_demo PRIVATE engine_shared)
target_compile_options(comp_demo PRIVATE -Wall -Wextra)
# Тест склада ассетов (CapCut-модель): дедуп, резолв, байты.
add_executable(asset_test asset_test.c)
target_include_directories(asset_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asset_test PRIVATE engine_shared)
target_compile_options(asset_test PRIVATE -Wall -Wextra)
# Пруф встраивания: dlopen libengine.so как редактор (линк только dl).
add_executable(dlopen_test dlopen_test.c)
target_include_directories(dlopen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(dlopen_test PRIVATE -Wall -Wextra)
# Тест сцены: иерархия, трансформы, пул. Без GPU.
add_executable(scene_test scene_test.c scene.c)
target_include_directories(scene_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scene_test PRIVATE engine_shared m)
target_compile_options(scene_test PRIVATE -Wall -Wextra)
# Headless-тест вьювера: окно+ядро+кадр в CPU (то, что ест Electron).
add_executable(view_test view_test.c)
# Пруф единой глубины: сфера спереди в обоих порядках проходов.
add_executable(depth_test depth_test.c)
target_include_directories(depth_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(depth_test PRIVATE engine_shared m)
target_compile_options(depth_test PRIVATE -Wall -Wextra)
target_include_directories(view_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(view_test PRIVATE engine_shared m)
target_compile_options(view_test PRIVATE -Wall -Wextra)
# Фаза 1 шва EngUi: своё окно, куб + 3 прямоугольника, клик = смена цвета.
add_executable(ui_test ui/ui_test.c)
target_include_directories(ui_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_test PRIVATE engine_shared m)
target_compile_options(ui_test PRIVATE -Wall -Wextra)
# Отдельный правщик фронта (§13): своё окно, без 3D, всегда EDIT.
add_executable(ui_edit ui_edit.c)
target_include_directories(ui_edit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_edit PRIVATE engine_shared m)
target_compile_options(ui_edit PRIVATE -Wall -Wextra)
# Прямой вьювер (схема Roblox/UE5): отдельный процесс, своё окно, GPU->GPU.
add_executable(direct_view direct_view.c)
target_include_directories(direct_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(direct_view PRIVATE engine_shared m)
target_compile_options(direct_view PRIVATE -Wall -Wextra)
