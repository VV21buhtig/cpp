cmake_minimum_required(VERSION 3.16)
project(VoxelRender LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

# --- Glad 2 ---
add_library(glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gl.c
)
target_include_directories(glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- Shader class ---
add_library(shader STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shader.cpp
)
target_include_directories(shader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(shader PUBLIC glad)

# --- Engine (mesh/texture) и Game (pick) — распил main.cpp, GL-порядок 1:1 ---
add_library(engine STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_chunk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_save.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_cvar.cpp
)
target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine PUBLIC glad glfw OpenGL::GL)

add_library(game STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game_pick.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game_player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game_console.cpp
)
target_include_directories(game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- Executable ---
add_executable(voxel_render
    main.cpp
    src/stb_impl.cpp
)
target_link_libraries(voxel_render PRIVATE shader engine game glad glfw OpenGL::GL)

if (UNIX AND NOT APPLE)
    target_link_libraries(voxel_render PRIVATE dl)
endif()

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