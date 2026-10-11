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
# Живое ядро орфана: vec на RHI (первый переезд домена).
add_executable(vec_demo vec_demo.c vec_core.c corelib.c rhi_gl.c third-party/glad/gl.c)
target_include_directories(vec_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(vec_demo PRIVATE glfw OpenGL::GL m)
target_compile_options(vec_demo PRIVATE -Wall -Wextra)
# Песочница Noita-like: pix-ядро + клеточные частицы (первая L4-физика).
add_executable(sand_demo sand_demo.c pix_core.c corelib.c rhi_gl.c third-party/glad/gl.c)
target_include_directories(sand_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(sand_demo PRIVATE glfw OpenGL::GL m)
target_compile_options(sand_demo PRIVATE -Wall -Wextra)

# Живое ядро орфана 2: sdf на RHI (реймарш примитивов).
add_executable(sdf_demo sdf_demo.c sdf_core.c corelib.c rhi_gl.c third-party/glad/gl.c)
target_include_directories(sdf_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad)
target_link_libraries(sdf_demo PRIVATE glfw OpenGL::GL m)
target_compile_options(sdf_demo PRIVATE -Wall -Wextra)
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
# Живое ядро орфана 3: webgpu-DDA на RHI (воксели, статический мир).
add_executable(vox_demo vox_demo.c webgpu_core.c corelib.c rhi_webgpu.c
  sky_lut.c vox/vox_gen.c vox/vox_tex.c
  ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h)
target_include_directories(vox_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_link_libraries(vox_demo PRIVATE glfw webgpu m)
target_compile_options(vox_demo PRIVATE -Wall -Wextra)
