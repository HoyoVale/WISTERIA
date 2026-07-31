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

# reusable project code
set(WISTERIA_CORE_SOURCES
    src/stb_image.cpp
    src/window.cpp
    src/shader.cpp
    src/vbo.cpp
    src/vao.cpp
    src/ebo.cpp
    src/texture.cpp
    src/camera.cpp
    src/timer.cpp
    src/model.cpp
    src/transform.cpp
    src/behaviour.cpp
    src/render_part.cpp
    src/model_asset.cpp
    src/entity.cpp
    src/scene.cpp
    src/renderer.cpp
    src/manager.cpp
    src/light.cpp
    src/Models/cube.cpp
    src/mesh.cpp
    src/material.cpp
)

add_library(wisteria_core STATIC ${WISTERIA_CORE_SOURCES})

target_include_directories(wisteria_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wisteria_core PUBLIC
    cxx_std_20
)

if(MSVC)
    target_compile_options(wisteria_core PRIVATE /utf-8)
endif()

target_precompile_headers(wisteria_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pch.hpp
)

target_compile_definitions(wisteria_core PUBLIC
    GLFW_INCLUDE_NONE
)

target_link_libraries(wisteria_core PUBLIC
    glfw
    glad
    glm_headers
)

# application
add_executable(wisteria src/main.cpp)
target_link_libraries(wisteria PRIVATE wisteria_core)

# automated tests
include(CTest)

if(BUILD_TESTING)
    add_executable(wisteria_tests tests/core_tests.cpp)
    target_link_libraries(wisteria_tests PRIVATE wisteria_core)

    if(MSVC)
        target_compile_options(wisteria_tests PRIVATE /utf-8)
    endif()

    add_test(NAME wisteria.core COMMAND wisteria_tests)
endif()
