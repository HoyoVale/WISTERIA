cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(FLORA LANGUAGES C CXX)

# 不编译 GLFW 自带的文档、测试和示例
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third-party/glfw)

if(MSVC)
    # GLFW compiles many translation units into one debug PDB. /FS protects
    # that PDB when CMake is built with --parallel.
    target_compile_options(glfw PRIVATE /FS)
endif()

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

# Assimp 5.2.5 is the newest release compatible with the project's current
# CMake 3.21 toolchain. Build only the static-model importers used here.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MMD_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    assimp
    URL https://github.com/assimp/assimp/archive/refs/tags/v5.2.5.tar.gz
    URL_HASH SHA256=b5219e63ae31d895d60d98001ee5bb809fb2c7b2de1e7f78ceeb600063641e1a
)
FetchContent_MakeAvailable(assimp)

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
    src/input.cpp
    src/timer.cpp
    src/model.cpp
    src/transform.cpp
    src/behaviour.cpp
    src/render_part.cpp
    src/model_asset.cpp
    src/importer.cpp
    src/entity.cpp
    src/scene.cpp
    src/renderer.cpp
    src/environment.cpp
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
    assimp
)

# application
add_executable(wisteria src/main.cpp)
target_link_libraries(wisteria PRIVATE wisteria_core)

# automated tests
include(CTest)

if(BUILD_TESTING)
    add_executable(wisteria_tests tests/core_tests.cpp)
    target_link_libraries(wisteria_tests PRIVATE wisteria_core)
    target_compile_definitions(wisteria_tests PRIVATE
        WISTERIA_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
        WISTERIA_PROJECT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    if(MSVC)
        target_compile_options(wisteria_tests PRIVATE /utf-8)
    endif()

    add_test(NAME wisteria.core COMMAND wisteria_tests)
endif()
