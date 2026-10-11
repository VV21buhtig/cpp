cmake_minimum_required(VERSION 3.20)
project(voxeditor LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF) # скелет без Q_OBJECT: только лямбды

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(glfw3 REQUIRED)

# Движок — готовой .so (не исходники!): редактор её грузит как продукт.
set(ENGINE_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../engine/build/libengine.so)
if(NOT EXISTS ${ENGINE_LIB})
  message(FATAL_ERROR "no libengine.so: собери engine (ebuild) прежде чем editor")
endif()

add_executable(editor main.cpp viewport.cpp panels.cpp)
target_include_directories(editor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../engine
  ${CMAKE_CURRENT_SOURCE_DIR}/../engine/third-party/glad)
target_link_libraries(editor PRIVATE
  ${ENGINE_LIB} Qt6::Widgets glfw)
target_compile_options(editor PRIVATE -Wall -Wextra)
# rpath чтобы .so нашлась рядом со сборкой без LD_LIBRARY_PATH
set_target_properties(editor PROPERTIES BUILD_RPATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../engine/build")
