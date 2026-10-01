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

# --- RmlUi (MIT): HTML/CSS UI, свой RenderInterface на нашем GL (их GL3-рендерер
# тащит свой glad — конфликт символов, не используем). Только Core, без сэмплов.
set(RMLUI_TMP_SHARED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third-party/rmlui)
set(BUILD_SHARED_LIBS ${RMLUI_TMP_SHARED})

# --- Dear ImGui (вендор, MIT): F1 дев-консоль (player-facing UI уезжает в RmlUi) ---
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/engine_light.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/rml_gl.cpp
)
target_include_directories(game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(game PUBLIC shader RmlUi::Core)

# --- Executable ---
add_executable(voxel_render
    main.cpp
    src/game/stb_impl.cpp
)
target_link_libraries(voxel_render PRIVATE shader engine game imgui glad glfw OpenGL::GL)

if (UNIX AND NOT APPLE)
    target_link_libraries(voxel_render PRIVATE dl)
endif()

# --- Данные рядом с бинарником. Через sync_data (ALL), а не POST_BUILD:
# POST_BUILD срабатывает только при перелинковке, и правка одного .rcss/.json
# без перекомпиляции оставляла в build/ несвежие файлы. sync_data едет каждый make.
add_custom_target(sync_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/structures.cfg
        ${CMAKE_CURRENT_BINARY_DIR}/structures.cfg
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/blocks.json
        ${CMAKE_CURRENT_BINARY_DIR}/blocks.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/sounds
        ${CMAKE_CURRENT_BINARY_DIR}/sounds
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${CMAKE_CURRENT_BINARY_DIR}/ui
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/texture
        ${CMAKE_CURRENT_BINARY_DIR}/texture
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        ${CMAKE_CURRENT_BINARY_DIR}/fonts
)
# Иконки хотбара (наши тайлы) рядом с RML-документами: ui/items/.
# Явный список вместо симлинков — детерминированно и дружит с паками.
set(HUD_ITEMS grass_side dirt stone log_side leaves ore_coal ore_iron ore_gold ore_diamond)
foreach(it ${HUD_ITEMS})
    add_custom_command(TARGET sync_data POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/texture/tiles/${it}.png
            ${CMAKE_CURRENT_BINARY_DIR}/ui/items/${it}.png
    )
endforeach()
add_custom_command(TARGET sync_data POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/texture/gui/widgets.png
        ${CMAKE_CURRENT_BINARY_DIR}/ui/widgets.png
)
add_dependencies(voxel_render sync_data)