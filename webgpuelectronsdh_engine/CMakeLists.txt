cmake_minimum_required(VERSION 3.20)
project(sdf_engine LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT EMSCRIPTEN)
  find_package(glfw3 REQUIRED)
  find_package(OpenGL REQUIRED)
  # glad из voxel-render без копий (ветка noflood): их gl.c + заголовки.
  add_library(glad STATIC ../voxel-render/src/engine/gl.c)
  target_include_directories(glad PUBLIC ../voxel-render/include)
  # Vulkan: системный libvulkan + заголовки из vulcan-render без копий.
  find_library(VULKAN_LIB vulkan REQUIRED)
  find_program(GLSLC glslc REQUIRED)
endif()

# CPU-каркас (окно+математика+арены), без GPU — только натив.
if(NOT EMSCRIPTEN)
add_executable(sdf_native csrc/main.c csrc/sdf_math.c)
target_include_directories(sdf_native PRIVATE csrc)
target_link_libraries(sdf_native PRIVATE glfw m)
target_compile_options(sdf_native PRIVATE -Wall -Wextra)
# Тест вокселей шага 1: генерация чанка, без рендера.
add_executable(vox_test csrc/vox_test.c csrc/vox/vox_gen.c csrc/vox/vox_mesh.c)
target_include_directories(vox_test PRIVATE csrc)
target_link_libraries(vox_test PRIVATE m)
target_compile_options(vox_test PRIVATE -Wall -Wextra)
# Тест матриц растровых ядер (GL/VK конвенции: глубина, lookAt).
add_executable(mat4_test csrc/core/mat4_test.c)
target_include_directories(mat4_test PRIVATE csrc)
target_link_libraries(mat4_test PRIVATE m)
target_compile_options(mat4_test PRIVATE -Wall -Wextra)
# SDF-демо: окно + sdf-ядро + сцена примитивов (натив, без webgpu).
add_executable(sdf_demo csrc/sdf_demo.c csrc/core/sdf_core.c csrc/core/corelib.c csrc/sdf_math.c)
target_include_directories(sdf_demo PRIVATE csrc)
target_link_libraries(sdf_demo PRIVATE glfw glad OpenGL::GL m)
target_compile_options(sdf_demo PRIVATE -Wall -Wextra)
# Тест SDF-бейка (точный EDT, без GPU).
add_executable(sdf_test csrc/vox_sdf_test.c csrc/vox/vox_gen.c csrc/vox/vox_sdf.c)
target_include_directories(sdf_test PRIVATE csrc)
target_link_libraries(sdf_test PRIVATE m)
target_compile_options(sdf_test PRIVATE -Wall -Wextra)
# Тест стриминга: кольцо + бюджет + выселение, без рендера.
add_executable(vox_stream csrc/vox_stream_test.c csrc/vox/vox_gen.c csrc/vox/vox_tex.c csrc/vox/vox_world.c csrc/vox/vox_world.c)
target_include_directories(vox_stream PRIVATE csrc)
target_link_libraries(vox_stream PRIVATE m)
target_compile_options(vox_stream PRIVATE -Wall -Wextra)
endif()

# WGSL-канон вшивается в заголовок при каждой правке shaders/sdf.wgsl.

# GPU-путь: натив — предсборка wgpu-native (без depot_tools/gn, качаем бинарники);
# веб — emdawnwebgpu-порт (тот же webgpu.h, тот же C-исходник).
if(EMSCRIPTEN)
  add_executable(sdf_native_webgpu csrc/wgpu_main.c csrc/core/webgpu_core.c csrc/sdf_math.c csrc/sky_lut.c csrc/sdf_settings.c csrc/vox/vox_gen.c csrc/vox/vox_tex.c csrc/vox/vox_world.c
    ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h)
  set_target_properties(sdf_native_webgpu PROPERTIES SUFFIX ".html")
  # Порты нужны и компиляции (заголовки), и линковке.
  target_compile_options(sdf_native_webgpu PRIVATE
    "--use-port=emdawnwebgpu" "-sUSE_GLFW=3")
  target_link_options(sdf_native_webgpu PRIVATE
    "--use-port=emdawnwebgpu" "-sUSE_GLFW=3" "-sALLOW_MEMORY_GROWTH=1" "-sDEFAULT_TO_CXX=1")
else()
  include(FetchContent)
  FetchContent_Declare(webgpu-distribution
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
    GIT_TAG dawn-7069)
  FetchContent_MakeAvailable(webgpu-distribution)
  # SPIR-V для vk-ядра из наших shaders/vk (glslc, без копий чужих бинарников).
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/spv_vk)
  foreach(SH vk_mesh.vert vk_mesh.frag vk_sky.vert vk_sky.frag)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/spv_vk/${SH}.spv
      COMMAND ${GLSLC} -O -o ${CMAKE_CURRENT_BINARY_DIR}/spv_vk/${SH}.spv
              ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vk/${SH}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vk/${SH}
      COMMENT "glslc ${SH}")
    list(APPEND VK_SPV ${CMAKE_CURRENT_BINARY_DIR}/spv_vk/${SH}.spv)
  endforeach()
  add_executable(sdf_native_webgpu csrc/wgpu_main.c csrc/core/webgpu_core.c csrc/core/corelib.c csrc/core/gl_core.c csrc/core/vk_core.c csrc/core/sdf_core.c csrc/sdf_math.c csrc/sky_lut.c csrc/sdf_settings.c csrc/vox/vox_gen.c csrc/vox/vox_tex.c csrc/vox/vox_world.c csrc/vox/vox_mesh.c
    ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h ${VK_SPV})
  target_compile_definitions(sdf_native_webgpu PRIVATE
    VK_SPV_DIR="${CMAKE_CURRENT_BINARY_DIR}/spv_vk")
  target_include_directories(sdf_native_webgpu PRIVATE
    ../vulcan-render/third-party/Vulkan-Headers)
  target_link_libraries(sdf_native_webgpu PRIVATE glfw webgpu glad OpenGL::GL ${VULKAN_LIB} m)
endif()
file(GLOB SDF_WGSL_PARTS
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sdf/*.wgsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky/*.wgsl")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h
  COMMAND ${CMAKE_COMMAND} -DSRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders
    -DDST=${CMAKE_CURRENT_BINARY_DIR}/gen/sdf_wgsl.h
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_wgsl.cmake
  DEPENDS ${SDF_WGSL_PARTS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_wgsl.cmake
  COMMENT "embed shaders (sdf+sky)")
target_include_directories(sdf_native_webgpu PRIVATE csrc ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_options(sdf_native_webgpu PRIVATE -Wall -Wextra)
if(NOT EMSCRIPTEN)
  target_link_libraries(sdf_native_webgpu PRIVATE glfw webgpu m)
endif()
