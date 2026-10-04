cmake_minimum_required(VERSION 3.20)
project(vulcan-render LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_program(GLSLC glslc REQUIRED)
find_program(GLSLANGVALIDATOR glslangValidator REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)
foreach(SH tri.vert tri.frag vk_terrain.vert vk_terrain.frag cull.comp shadow.vert shadow.frag dbg.frag sky.frag lum.comp adapt.comp tonemap.frag bright.comp kdown.comp kup.comp water.vert water.frag ssao.comp mv.comp)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SH}.spv
        COMMAND ${GLSLC} -O -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SH}.spv
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SH}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SH}
        COMMENT "glslc ${SH}")
    list(APPEND SHADERS_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SH}.spv)
endforeach()

# FSR2 (MIT, AMD): пермутации генерируем своим скриптом (ffx_sc — .exe, под
# Linux его нет; логика 1:1 из FSR2 vk/CMakeLists). Выход — заголовки в build.
set(FFX_PASSES
    ffx_fsr2_tcr_autogen_pass ffx_fsr2_autogen_reactive_pass
    ffx_fsr2_accumulate_pass ffx_fsr2_compute_luminance_pyramid_pass
    ffx_fsr2_depth_clip_pass ffx_fsr2_lock_pass
    ffx_fsr2_reconstruct_previous_depth_pass ffx_fsr2_rcas_pass)
set(FFX_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/ffx_gen)
file(MAKE_DIRECTORY ${FFX_GEN_DIR})
set(FFX_PERM_HEADERS "")
foreach(FFX_PASS ${FFX_PASSES})
    list(APPEND FFX_PERM_HEADERS ${FFX_GEN_DIR}/${FFX_PASS}_permutations.h)
endforeach()
file(GLOB FFX_SHADER_DEPS
     ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/api/shaders/*.glsl
     ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/api/shaders/*.h)
add_custom_command(OUTPUT ${FFX_PERM_HEADERS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/gen_permutations.py
            ${GLSLANGVALIDATOR} ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/api/shaders
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/api/vk/shaders
            ${FFX_GEN_DIR}/tmp ${FFX_GEN_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/gen_permutations.py ${FFX_SHADER_DEPS}
    COMMENT "fsr2 permutations")
add_custom_target(fsr2_permutations DEPENDS ${FFX_PERM_HEADERS})

add_library(ffsr2 STATIC
    third-party/ffx/api/ffx_fsr2.cpp
    third-party/ffx/api/ffx_assert.cpp
    third-party/ffx/api/vk/ffx_fsr2_vk.cpp
    third-party/ffx/api/vk/shaders/ffx_fsr2_shaders_vk.cpp)
add_dependencies(ffsr2 fsr2_permutations)
target_include_directories(ffsr2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/api
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx
    ${FFX_GEN_DIR})
target_compile_options(ffsr2 PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated-declarations>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-include>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:${CMAKE_CURRENT_SOURCE_DIR}/third-party/ffx/ffx_wcscpy_compat.h>)
target_compile_definitions(ffsr2 PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:FFX_GCC>)
target_link_libraries(ffsr2 PUBLIC Vulkan::Vulkan)

# Форк движка: СВОИ копии engine-файлов (vulcan для v4, GL для noflood —
# друг друга не тащат). stb_image — системный (/usr/include).
add_executable(vulcan_render
    src/main.cpp
    src/vk/bootstrap.cpp
    src/vk/targets.cpp
    src/vk/descriptors.cpp
    src/vk/pipelines.cpp
    src/vk/frame.cpp
    src/vk/fsr2.cpp
    src/sky_atmo.cpp
    src/mesh_vk.cpp
    src/vma_impl.cpp
    src/engine/engine_world.cpp
    src/engine/engine_light.cpp
    src/engine/engine_blocks.cpp
    src/engine/struct_config.cpp
    src/engine/struct_tree.cpp
    src/engine/struct_rock.cpp
    src/engine/struct_mine.cpp
    src/game/stb_impl.cpp
    ${SHADERS_SPV})
target_include_directories(vulcan_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/Vulkan-Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vulcan_render PRIVATE Vulkan::Vulkan glfw ffsr2)
target_compile_definitions(vulcan_render PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:FFX_GCC>)
add_dependencies(vulcan_render fsr2_permutations)
target_compile_definitions(vulcan_render PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders/")
if(MSVC)
  target_compile_options(vulcan_render PRIVATE /W4)
  # VMA — чужая стена варнингов, гасим (наш код остаётся на /W4)
  set_source_files_properties(src/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  target_compile_options(vulcan_render PRIVATE -Wall -Wextra)
  # VMA — чужая стена варнингов, гасим (наш код остаётся на -Wall)
  set_source_files_properties(src/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
