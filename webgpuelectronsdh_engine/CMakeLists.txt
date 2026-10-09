cmake_minimum_required(VERSION 3.20)
project(sdf_engine LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)

# CPU-каркас (окно+математика+арены), без GPU — собирается всегда и везде.
add_executable(sdf_native csrc/main.c csrc/sdf_math.c)
target_include_directories(sdf_native PRIVATE csrc)
target_link_libraries(sdf_native PRIVATE glfw m)
target_compile_options(sdf_native PRIVATE -Wall -Wextra)

# GPU-путь: Dawn через предсборку (без depot_tools/gn, качаем бинарники).
# Тег dawn-XXXX = снапшот Dawn с готовыми .so. Веб-таргет — когда будет emsdk.
include(FetchContent)
FetchContent_Declare(webgpu-distribution
  GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
  GIT_TAG dawn-7069)
FetchContent_MakeAvailable(webgpu-distribution)

add_executable(sdf_native_webgpu csrc/wgpu_main.c csrc/sdf_math.c
  ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/shaders/sdf.wgsl
    -DDST=${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_wgsl.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sdf.wgsl
  COMMENT "embed sdf.wgsl")
target_include_directories(sdf_native_webgpu PRIVATE csrc ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_link_libraries(sdf_native_webgpu PRIVATE glfw webgpu m)
target_compile_options(sdf_native_webgpu PRIVATE -Wall -Wextra)
