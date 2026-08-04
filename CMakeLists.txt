cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(FLORA LANGUAGES C CXX)

# The native C ABI library is a shared object on Linux/macOS; every static
# dependency it links must be compiled with position-independent code.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(WISTERIA_BUILD_NATIVE
    "Build the experimental native C ABI shared library (wisteria_native)"
    OFF
)
option(WISTERIA_BUILD_SABA_VIEWER
    "Build Saba's standalone reference OpenGL viewer"
    OFF
)

# GLFW 3.4 enables X11 and Wayland together by default. That makes a Linux
# build depend on both complete development stacks even though WISTERIA only
# needs one window backend. X11 is the conservative default and works through
# XWayland on WSLg. NULL is a compile/test backend with no desktop window.
set(WISTERIA_LINUX_WINDOW_BACKEND "X11" CACHE STRING
    "Linux GLFW backend: X11, WAYLAND, BOTH, or NULL")
set_property(CACHE WISTERIA_LINUX_WINDOW_BACKEND PROPERTY STRINGS
    X11 WAYLAND BOTH NULL)
if(UNIX AND NOT APPLE)
    string(TOUPPER "${WISTERIA_LINUX_WINDOW_BACKEND}"
        WISTERIA_LINUX_WINDOW_BACKEND_NORMALIZED)
    if(WISTERIA_LINUX_WINDOW_BACKEND_NORMALIZED STREQUAL "X11")
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    elseif(WISTERIA_LINUX_WINDOW_BACKEND_NORMALIZED STREQUAL "WAYLAND")
        set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    elseif(WISTERIA_LINUX_WINDOW_BACKEND_NORMALIZED STREQUAL "BOTH")
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    elseif(WISTERIA_LINUX_WINDOW_BACKEND_NORMALIZED STREQUAL "NULL")
        set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    else()
        message(FATAL_ERROR
            "WISTERIA_LINUX_WINDOW_BACKEND must be X11, WAYLAND, BOTH, or NULL")
    endif()
endif()

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

# Assimp 5.2.5 is vendored so a clean build never depends on GitHub access.
# Build only the static importers used by this project.
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

add_subdirectory(third-party/assimp)

# Bullet Physics 3.25 is vendored explicitly by script/setup_bullet.ps1.
# CMake never downloads dependencies so clean builds remain deterministic.
set(WISTERIA_BULLET_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third-party/bullet3")
if(NOT EXISTS "${WISTERIA_BULLET_ROOT}/src/btBulletDynamicsCommon.h")
    message(FATAL_ERROR
        "Bullet Physics 3.25 is missing. Run .\\script\\setup_bullet.ps1 "
        "from the WISTERIA project root, then configure again.")
endif()
if(NOT EXISTS "${WISTERIA_BULLET_ROOT}/VERSION")
    message(FATAL_ERROR "Bullet source is incomplete: VERSION is missing.")
endif()
file(STRINGS "${WISTERIA_BULLET_ROOT}/VERSION"
    WISTERIA_BULLET_VERSION LIMIT_COUNT 1)
if(NOT WISTERIA_BULLET_VERSION STREQUAL "3.25")
    message(FATAL_ERROR
        "WISTERIA requires Bullet 3.25, found ${WISTERIA_BULLET_VERSION}.")
endif()

set(BULLET_PHYSICS_SOURCE_DIR "${WISTERIA_BULLET_ROOT}")
set(BULLET_VERSION "3.25")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
set(USE_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)

# Build only the Bullet 2 rigid-body libraries required by WISTERIA.
add_subdirectory(
    "${WISTERIA_BULLET_ROOT}/src/LinearMath"
    "${CMAKE_BINARY_DIR}/third-party/bullet3/LinearMath"
    EXCLUDE_FROM_ALL
)
add_subdirectory(
    "${WISTERIA_BULLET_ROOT}/src/BulletCollision"
    "${CMAKE_BINARY_DIR}/third-party/bullet3/BulletCollision"
    EXCLUDE_FROM_ALL
)
add_subdirectory(
    "${WISTERIA_BULLET_ROOT}/src/BulletDynamics"
    "${CMAKE_BINARY_DIR}/third-party/bullet3/BulletDynamics"
    EXCLUDE_FROM_ALL
)

if(MSVC)
    foreach(WISTERIA_BULLET_TARGET LinearMath BulletCollision BulletDynamics)
        target_compile_options(${WISTERIA_BULLET_TARGET} PRIVATE /FS)
        set_property(TARGET ${WISTERIA_BULLET_TARGET} PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endforeach()
endif()

# Saba (MIT) vendored MMD runtime. Reference implementation for the MMD
# adapter rewrite; only the core Base + MMD library is built.
include(third-party/saba/WISTERIA.cmake)

# reusable project code, grouped by architecture module
set(WISTERIA_COMMON_SOURCES
    src/common/stb_image.cpp
    src/common/miniz.cpp
)

set(WISTERIA_CORE_SOURCES
    src/core/timer.cpp
    src/core/transform.cpp
    src/core/root_motion.cpp
)

set(WISTERIA_RUNTIME_SOURCES
    src/runtime/saba_mmd_runtime_model.cpp
)

set(WISTERIA_PLATFORM_SOURCES
    src/platform/application.cpp
    src/platform/input.cpp
    src/platform/window.cpp
    src/platform/window_manager.cpp
)

set(WISTERIA_RENDERING_SOURCES
    src/rendering/camera.cpp
    src/rendering/ebo.cpp
    src/rendering/environment.cpp
    src/rendering/framebuffer.cpp
    src/rendering/light.cpp
    src/rendering/material.cpp
    src/rendering/program_cache.cpp
    src/rendering/mesh.cpp
    src/rendering/model.cpp
    src/rendering/primitives/cube.cpp
    src/rendering/render_part.cpp
    src/rendering/renderer.cpp
    src/rendering/shader.cpp
    src/rendering/texture.cpp
    src/rendering/vao.cpp
    src/rendering/vbo.cpp
)

set(WISTERIA_ANIMATION_SOURCES
    src/animation/animation.cpp
    src/animation/animation_state_machine.cpp
    src/animation/animator.cpp
    src/animation/morph.cpp
    src/animation/pose.cpp
    src/animation/pose_buffer.cpp
    src/animation/skeleton.cpp
)

set(WISTERIA_ASSET_SOURCES
    src/assets/importer.cpp
    src/assets/manager.cpp
    src/assets/model_asset.cpp
    src/assets/saba_mmd_importer.cpp
)

set(WISTERIA_PHYSICS_SOURCES
    src/physics/physics_bullet_conversion.cpp
    src/physics/physics_types.cpp
    src/physics/physics_world.cpp
)

set(WISTERIA_MMD_SOURCES
    src/mmd/mmd_pose_solver.cpp
    src/mmd/vmd_importer.cpp
    src/mmd/physics/mmd_physics_asset.cpp
)

set(WISTERIA_SCENE_SOURCES
    src/scene/behaviour.cpp
    src/scene/demo_scene.cpp
    src/scene/entity.cpp
    src/scene/scene.cpp
)

add_library(wisteria_core STATIC
    ${WISTERIA_COMMON_SOURCES}
    ${WISTERIA_CORE_SOURCES}
    ${WISTERIA_RUNTIME_SOURCES}
    ${WISTERIA_PLATFORM_SOURCES}
    ${WISTERIA_RENDERING_SOURCES}
    ${WISTERIA_ANIMATION_SOURCES}
    ${WISTERIA_ASSET_SOURCES}
    ${WISTERIA_PHYSICS_SOURCES}
    ${WISTERIA_MMD_SOURCES}
    ${WISTERIA_SCENE_SOURCES}
)

target_include_directories(wisteria_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${WISTERIA_BULLET_ROOT}/src
)

target_compile_features(wisteria_core PUBLIC
    cxx_std_20
)

if(MSVC)
    # MSBuild may launch several cl.exe processes for this target. /FS
    # serializes writes to the shared program database and prevents C1041.
    target_compile_options(wisteria_core PRIVATE /utf-8 /FS)
endif()

target_precompile_headers(wisteria_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/wisteria/common/pch.hpp
)

target_compile_definitions(wisteria_core PUBLIC
    GLFW_INCLUDE_NONE
    WISTERIA_BUILD_CONFIGURATION="$<CONFIG>"
)

target_link_libraries(wisteria_core PUBLIC
    glfw
    glad
    glm_headers
    assimp
    saba
    BulletDynamics
    BulletCollision
    LinearMath
)

# native C ABI shared library for FFI / frontend integration
if(WISTERIA_BUILD_NATIVE)
    add_library(wisteria_native SHARED
        src/native/internal/native_context.cpp
        src/native/native_common.cpp
        src/native/native_model.cpp
        src/native/native_window.cpp
        src/native/windows_path.cpp
    )
    target_include_directories(wisteria_native PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(wisteria_native PRIVATE cxx_std_20)
    target_link_libraries(wisteria_native PRIVATE wisteria_core)
    target_compile_definitions(wisteria_native PRIVATE WISTERIA_NATIVE_BUILD)
    set_target_properties(wisteria_native PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    if(MSVC)
        target_compile_options(wisteria_native PRIVATE /utf-8 /FS)
    endif()
endif()

# application
add_executable(wisteria src/main.cpp)
target_link_libraries(wisteria PRIVATE wisteria_core)

# automated tests
include(CTest)

if(BUILD_TESTING)
    add_executable(wisteria_tests tests/core_tests.cpp)
    target_link_libraries(wisteria_tests PRIVATE wisteria_core)
    if(WISTERIA_BUILD_NATIVE)
        target_link_libraries(wisteria_tests PRIVATE wisteria_native)
        target_compile_definitions(wisteria_tests PRIVATE
            WISTERIA_TEST_NATIVE_ABI=1
        )
    endif()
    target_compile_definitions(wisteria_tests PRIVATE
        WISTERIA_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
        WISTERIA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
        WISTERIA_PROJECT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

    if(MSVC)
        target_compile_options(wisteria_tests PRIVATE /utf-8)
    endif()

    add_test(NAME wisteria.core COMMAND wisteria_tests)

    # Minimal render regression: creates a real GL context and verifies that
    # the engine SceneFramebuffer clears and reads back two consecutive
    # frames. Skips when no display backend is available (Linux NULL build).
    add_executable(wisteria_render_tests tests/render_fbo_tests.cpp)
    target_link_libraries(wisteria_render_tests PRIVATE wisteria_core)
    if(MSVC)
        target_compile_options(wisteria_render_tests PRIVATE /utf-8)
    endif()
    add_test(NAME wisteria.render-fbo COMMAND wisteria_render_tests)
endif()
