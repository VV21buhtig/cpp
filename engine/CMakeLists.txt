cmake_minimum_required(VERSION 3.20)
project(voxengine LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)
find_library(VULKAN_LIB vulkan REQUIRED)
include(FetchContent)
FetchContent_Declare(webgpu-distribution
  GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
  GIT_TAG dawn-7069)
FetchContent_MakeAvailable(webgpu-distribution)

# База инструмента: окно+ввод+цикл, слепая к контенту. Ядра линкует игра.
add_executable(engine_test engine_test.c engine.c)
# render_api.h тянет sdf_math.h/vox_chunk.h из игры (математика переедет
# в engine/ вместе с игровым кодом — сейчас только путь, не копия).
# render_api.h самодостаточен (vec.h + fwd VoxChunk): путей в игру нет.
target_include_directories(engine_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_test PRIVATE glfw m)
target_compile_options(engine_test PRIVATE -Wall -Wextra)

# Тест математики движка (переехал из игры).
add_executable(mat4_test mat4_test.c)
target_include_directories(mat4_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mat4_test PRIVATE m)
target_compile_options(mat4_test PRIVATE -Wall -Wextra)
# Тест композитора L3: порядок + отказы, без GPU.
add_executable(comp_test comp_test.c comp.c)
target_include_directories(comp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(comp_test PRIVATE -Wall -Wextra)
# Тест RHI: GL-контекст + VK-девайс, без контента.
add_executable(rhi_test rhi_test.c rhi_vk.c rhi_gl.c third-party/glad/gl.c)
target_include_directories(rhi_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/Vulkan-Headers)
target_link_libraries(rhi_test PRIVATE glfw ${VULKAN_LIB} m)
target_compile_options(rhi_test PRIVATE -Wall -Wextra)
# Песочница Noita-like: pix-ядро + клеточные частицы (первая L4-физика).
add_executable(sand_demo ../template/sand_demo.c pix_core.c corelib.c rhi_gl.c third-party/glad/gl.c)
target_include_directories(sand_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(sand_demo PRIVATE glfw OpenGL::GL m)
target_compile_options(sand_demo PRIVATE -Wall -Wextra)

# WGSL-канон вшивается в заголовок при каждой правке шейдеров.
file(GLOB SDF_WGSL_PARTS
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sdf/*.wgsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky/*.wgsl")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h
  COMMAND ${CMAKE_COMMAND} -DSRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders
    -DDST=${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_wgsl.cmake
  DEPENDS ${SDF_WGSL_PARTS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_wgsl.cmake
  COMMENT "embed shaders (sdf+sky)")
# Тест L4: гравитация + часы фиксированного шага, чистый CPU.
add_executable(phys_test phys_test.c)
target_include_directories(phys_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phys_test PRIVATE m)
target_compile_options(phys_test PRIVATE -Wall -Wextra)
# Lua-демо: геймплей на скриптах (vec.lua), рендер в движке.
add_executable(lua_demo ../template/lua_demo.c vec_core.c corelib.c rhi_gl.c third-party/glad/gl.c)
target_include_directories(lua_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(lua_demo PRIVATE glfw OpenGL::GL lua m)
target_compile_options(lua_demo PRIVATE -Wall -Wextra)
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
# Общая 3D-сцена: одна на всех (террейн + SDF-витрина + куб).
# Линкует все 3D-ядра: выбор через --core.
add_executable(scene3d ../template/scene3d.c gl_core.c vk_core.c webgpu_core.c sdf_core.c mesh_core.c corelib.c rhi_gl.c rhi_vk.c rhi_webgpu.c vox/vox_gen.c vox/vox_mesh.c vox/vox_tex.c vox_feed.c sky_lut.c third-party/glad/gl.c
  ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h ${VK_SPV})
target_compile_definitions(scene3d PRIVATE
  VK_SPV_DIR="${CMAKE_CURRENT_BINARY_DIR}/spv_vk")
target_include_directories(scene3d PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/Vulkan-Headers
  ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_link_libraries(scene3d PRIVATE glfw OpenGL::GL ${VULKAN_LIB} webgpu m)
target_compile_options(scene3d PRIVATE -Wall -Wextra)
# Общая 2D-сцена: фигуры для vec-ядра.
add_executable(scene2d ../template/scene2d.c vec_core.c corelib.c rhi_gl.c third-party/glad/gl.c)
target_include_directories(scene2d PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(scene2d PRIVATE glfw OpenGL::GL m)
target_compile_options(scene2d PRIVATE -Wall -Wextra)
# Композитное демо L3: воксели + векторный UI в одном кадре (общая цель).
add_executable(comp_demo ../template/comp_demo.c gl_core.c vec_core.c corelib.c comp.c rhi_gl.c vox/vox_gen.c vox/vox_mesh.c vox/vox_tex.c vox_feed.c third-party/glad/gl.c)
target_include_directories(comp_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(comp_demo PRIVATE glfw OpenGL::GL m)
target_compile_options(comp_demo PRIVATE -Wall -Wextra)
# Тест склада ассетов (CapCut-модель): дедуп, резолв, байты.
add_executable(asset_test asset_test.c asset.c)
target_include_directories(asset_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(asset_test PRIVATE -Wall -Wextra)
