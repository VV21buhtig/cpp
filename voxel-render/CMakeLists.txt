cmake_minimum_required(VERSION 3.16)
project(VoxelRender LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

# --- Glad 2 ---
add_library(glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gl.c
)
target_include_directories(glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- Shader class ---
add_library(shader STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/shader.cpp
)
target_include_directories(shader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(shader PUBLIC glad)

# --- Dear ImGui (вендор, MIT): консоль и крутилки света ---
add_library(imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui
)
target_link_libraries(imgui PUBLIC glfw)

# --- Engine (mesh/texture) и Game (pick) — распил main.cpp, GL-порядок 1:1 ---
add_library(engine STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/engine_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/engine_chunk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/engine_world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/engine_save.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/engine_cvar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/engine_blocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/audio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/struct/struct_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/struct/struct_tree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/struct/struct_mine.cpp
)
target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine PUBLIC glad glfw OpenGL::GL pthread dl m)

add_library(game STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game_player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/game_console.cpp
)
target_include_directories(game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- Executable ---
add_executable(voxel_render
    main.cpp
    src/game/stb_impl.cpp
)
target_link_libraries(voxel_render PRIVATE shader engine game imgui glad glfw OpenGL::GL)

if (UNIX AND NOT APPLE)
    target_link_libraries(voxel_render PRIVATE dl)
endif()

add_custom_command(TARGET voxel_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/structures.cfg
        $<TARGET_FILE_DIR:voxel_render>/structures.cfg
)
add_custom_command(TARGET voxel_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/blocks.json
        $<TARGET_FILE_DIR:voxel_render>/blocks.json
)
add_custom_command(TARGET voxel_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/sounds
        $<TARGET_FILE_DIR:voxel_render>/sounds
)
# --- Копируем shaders/ и texture/ рядом с бинарником после сборки ---
add_custom_command(TARGET voxel_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:voxel_render>/shaders
)
add_custom_command(TARGET voxel_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/texture
        $<TARGET_FILE_DIR:voxel_render>/texture
)
add_custom_command(TARGET voxel_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        $<TARGET_FILE_DIR:voxel_render>/fonts
)