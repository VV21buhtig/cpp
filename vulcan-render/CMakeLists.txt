cmake_minimum_required(VERSION 3.20)
project(vulcan-render LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_program(GLSLC glslc REQUIRED)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)
foreach(SH tri.vert tri.frag vk_terrain.vert vk_terrain.frag cull.comp shadow.vert shadow.frag dbg.frag sky.frag lum.comp adapt.comp tonemap.frag bright.comp kdown.comp kup.comp)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SH}.spv
        COMMAND ${GLSLC} -O -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SH}.spv
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SH}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SH}
        COMMENT "glslc ${SH}")
    list(APPEND SHADERS_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SH}.spv)
endforeach()

# Форк движка: СВОИ копии engine-файлов (vulcan для v4, GL для noflood —
# друг друга не тащат). stb_image — системный (/usr/include).
add_executable(vulcan_render
    src/main.cpp
    src/mesh_vk.cpp
    src/vma_impl.cpp
    src/engine/engine_world.cpp
    src/engine/engine_light.cpp
    src/engine/engine_blocks.cpp
    src/engine/struct_config.cpp
    src/engine/struct_tree.cpp
    src/engine/struct_mine.cpp
    src/game/stb_impl.cpp
    ${SHADERS_SPV})
target_include_directories(vulcan_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/Vulkan-Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vulcan_render PRIVATE Vulkan::Vulkan glfw)
target_compile_definitions(vulcan_render PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders/")
target_compile_options(vulcan_render PRIVATE -Wall -Wextra)
# VMA — чужая стена варнингов, гасим (наш код остаётся на -Wall)
set_source_files_properties(src/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
