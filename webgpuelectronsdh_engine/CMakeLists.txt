cmake_minimum_required(VERSION 3.20)
project(sdf_engine LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)

add_executable(sdf_native csrc/main.c csrc/sdf_math.c)
target_include_directories(sdf_native PRIVATE csrc)
target_link_libraries(sdf_native PRIVATE glfw m)
target_compile_options(sdf_native PRIVATE -Wall -Wextra)

# Веб-таргет появится когда будет emsdk:
# if(EMSCRIPTEN)
#   set_target_properties(sdf_web PROPERTIES SUFFIX ".html")
#   target_link_libraries(sdf_web PRIVATE emdawnwebgpu_cpp webgpu_glfw)
#   target_link_options(sdf_web PRIVATE "-sASYNCIFY=1" "-sUSE_GLFW=3")
# endif()
# Dawn-native (webgpu_dawn) — следующим коммитом через FetchContent.
