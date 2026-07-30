cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(FLORA LANGUAGES C CXX)

# 不编译 GLFW 自带的文档、测试和示例
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third-party/glfw)

# glad
add_library(glad STATIC
    third-party/glad/src/gl.c
)

target_include_directories(glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad/include
)

# glm
add_library(glm_headers INTERFACE)

target_include_directories(glm_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glm
)

#stb_image
add_library(stb INTERFACE)

target_include_directories(stb INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/stb/include
)

# main
add_executable(wisteria
    src/main.cpp
    src/window.cpp
    src/shader.cpp
    src/vbo.cpp
    src/vao.cpp
    src/ebo.cpp
)

target_include_directories(wisteria PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wisteria PRIVATE
    cxx_std_20
)

target_precompile_headers(wisteria PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pch.hpp
)

target_compile_definitions(wisteria PRIVATE
    GLFW_INCLUDE_NONE
    STB_IMAGE_IMPLEMENTATION
)

target_link_libraries(wisteria PRIVATE
    glfw
    glad
    glm_headers
)