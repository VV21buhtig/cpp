cmake_minimum_required(VERSION 3.20)
project(voxengine LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)

# База инструмента: окно+ввод+цикл, слепая к контенту. Ядра линкует игра.
add_executable(engine_test engine_test.c engine.c)
# render_api.h тянет sdf_math.h/vox_chunk.h из игры (математика переедет
# в engine/ вместе с игровым кодом — сейчас только путь, не копия).
target_include_directories(engine_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../webgpuelectronsdh_engine/csrc)
target_link_libraries(engine_test PRIVATE glfw m)
target_compile_options(engine_test PRIVATE -Wall -Wextra)

# Тест математики движка (переехал из игры).
add_executable(mat4_test mat4_test.c)
target_include_directories(mat4_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mat4_test PRIVATE m)
target_compile_options(mat4_test PRIVATE -Wall -Wextra)
