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

# --- Executable ---
add_executable(voxel_render
    main.cpp
    src/stb_impl.cpp
)
target_link_libraries(voxel_render PRIVATE shader glad glfw OpenGL::GL)

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