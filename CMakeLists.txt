cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(FLORA LANGUAGES C CXX)

# The native C ABI library is a shared object on Linux/macOS; every static
# dependency it links must be compiled with position-independent code.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(WISTERIA_BUILD_NATIVE
    "Build the experimental native C ABI shared library (wisteria_native)"
    ON
)
option(WISTERIA_BUILD_SABA_VIEWER
    "Build Saba's standalone reference OpenGL viewer"
    OFF
)
option(WISTERIA_TEST_FULL_ASSETS
    "Treat project MMD/VMD assets as required test fixtures (FAIL on missing)"
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

# glm — WISTERIA owns the single math ABI.
#
# GLM is header-only and emits identical mangled inline symbols across
# versions, so the whole process must see exactly ONE GLM header set. The
# engine owns GLM 1.0.3 (third-party/glm) and the Saba adapter links this
# same interface target instead of exposing its bundled GLM 0.9.x. This keeps
# the math ABI a WISTERIA decision: backends adapt to the engine, never the
# other way around. Mixing two GLM versions caused an ODR collision that made
# matrix multiplication read the wrong layout (reproduced with ASan -O1 and
# GCC 14).
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
    src/runtime/runtime_model_base.cpp
    src/runtime/model_backend.cpp
    src/runtime/model_instance.cpp
    src/runtime/saba_mmd_runtime_model.cpp
)

set(WISTERIA_PLATFORM_SOURCES
    src/platform/application.cpp
    src/platform/free_camera_controller.cpp
    src/platform/input.cpp
    src/platform/window.cpp
    src/platform/window_manager.cpp
)

set(WISTERIA_RENDERING_SOURCES
    src/rendering/camera.cpp
    src/rendering/ebo.cpp
    src/rendering/environment.cpp
    src/rendering/framebuffer.cpp
    src/rendering/graphics_device.cpp
    src/rendering/light.cpp
    src/rendering/material.cpp
    src/rendering/program_cache.cpp
    src/rendering/mesh.cpp
    src/rendering/model.cpp
    src/rendering/primitives/cube.cpp
    src/rendering/primitives/procedural.cpp
    src/rendering/render_part.cpp
    src/rendering/render_morphing.cpp
    src/rendering/render_passes.cpp
    src/rendering/render_physics_debug.cpp
    src/rendering/render_present.cpp
    src/rendering/render_skinning.cpp
    src/rendering/render_state.cpp
    src/rendering/render_uniforms.cpp
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
    src/assets/pmx_parser.cpp
    src/assets/saba_mmd_importer.cpp
)

set(WISTERIA_PHYSICS_SOURCES
    src/physics/bullet_body.cpp
    src/physics/bullet_constraint.cpp
    src/physics/bullet_debug_draw.cpp
    src/physics/bullet_stepper.cpp
    src/physics/physics_bullet_conversion.cpp
    src/physics/physics_types.cpp
    src/physics/physics_world.cpp
)

set(WISTERIA_MMD_SOURCES
    src/mmd/mmd_determinism.cpp
    src/mmd/mmd_pose_solver.cpp
    src/mmd/mmd_camera_conversion.cpp
    src/mmd/mmd_light_conversion.cpp
    src/mmd/vmd_importer.cpp
    src/mmd/physics/mmd_physics_audit.cpp
    src/mmd/physics/mmd_physics_asset.cpp
    src/mmd/physics/mmd_physics_configuration.cpp
)

set(WISTERIA_SCENE_SOURCES
    src/scene/behaviour.cpp
    src/scene/entity.cpp
    src/scene/scene.cpp
)

add_library(wisteria_core STATIC
    ${WISTERIA_COMMON_SOURCES}
    ${WISTERIA_CORE_SOURCES}
    ${WISTERIA_RUNTIME_SOURCES}
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
    WISTERIA_BUILD_CONFIGURATION="$<CONFIG>"
)

target_link_libraries(wisteria_core PUBLIC
    glad
    glm_headers
    assimp
    saba
    BulletDynamics
    BulletCollision
    LinearMath
)

# GLFW-bound platform layer: windowing, input callbacks, the Application frame
# loop and the Saba demo scene. Kept out of wisteria_core so the core library
# compiles and links without GLFW (headless/NULL backend, tests, tooling).
add_library(wisteria_platform STATIC
    ${WISTERIA_PLATFORM_SOURCES}
    src/scene/demo_scene.cpp
)

target_link_libraries(wisteria_platform PUBLIC
    wisteria_core
    glfw
)

target_compile_features(wisteria_platform PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(wisteria_platform PRIVATE /utf-8 /FS)
endif()

target_precompile_headers(wisteria_platform PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/wisteria/common/pch.hpp
)

target_compile_definitions(wisteria_platform PUBLIC GLFW_INCLUDE_NONE)

# native C ABI shared library for FFI / frontend integration
if(WISTERIA_BUILD_NATIVE)
    add_library(wisteria_native SHARED
        src/native/internal/native_context.cpp
        src/native/native_common.cpp
        src/native/native_model.cpp
        src/native/native_scene.cpp
        src/native/native_window.cpp
        src/native/windows_path.cpp
    )
    target_include_directories(wisteria_native PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(wisteria_native PRIVATE cxx_std_20)
    target_link_libraries(wisteria_native PRIVATE
        wisteria_core
        wisteria_platform
    )
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
target_link_libraries(wisteria PRIVATE wisteria_platform)

# R1.3 trace tooling: JSONL writer/parser and the minimal diff CLI. The
# runtime never performs trace I/O (contract §6.4); tooling lives here.
add_library(wisteria_trace_tooling STATIC
    tools/trace/trace_jsonl.cpp
)
target_include_directories(wisteria_trace_tooling PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/nlohmann
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/trace
)
target_link_libraries(wisteria_trace_tooling PUBLIC wisteria_core)

add_executable(wisteria_trace_diff
    tools/trace/trace_diff_main.cpp
)
target_link_libraries(wisteria_trace_diff PRIVATE wisteria_trace_tooling)

add_executable(wisteria_trace_export
    tools/trace/trace_export_main.cpp
)
target_link_libraries(wisteria_trace_export PRIVATE wisteria_trace_tooling)

# automated tests
include(CTest)

if(BUILD_TESTING)
    # R1.2B test hooks (StepRestoredPhysicsForProbe / fault injection) are
    # compiled into wisteria_core and propagate to test binaries. They are
    # inert without explicit calls and never appear in non-testing builds.
    target_compile_definitions(wisteria_core PUBLIC
        WISTERIA_DETERMINISM_TEST_HOOKS=1
    )
    # Test pyramid: pure logic (unit), engine runtime without windows (runtime),
    # asset/importer/Saba/native integration (integration), and the render
    # regression below. Each tier builds and runs independently.
    foreach(TIER unit runtime integration)
        add_executable(wisteria_${TIER}_tests tests/${TIER}_tests.cpp)
        target_link_libraries(wisteria_${TIER}_tests PRIVATE
            wisteria_platform
            wisteria_trace_tooling
        )
        target_compile_definitions(wisteria_${TIER}_tests PRIVATE
            WISTERIA_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
            WISTERIA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
            WISTERIA_PROJECT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
            WISTERIA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fixtures.json"
        )
        target_include_directories(wisteria_${TIER}_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/nlohmann
        )
        if(MSVC)
            target_compile_options(wisteria_${TIER}_tests PRIVATE /utf-8)
        endif()
        add_test(NAME wisteria.${TIER} COMMAND wisteria_${TIER}_tests)
    endforeach()
    # The linked-body A/B semantic test drives Bullet directly (the engine
    # wrappers intentionally hide bt* from public headers).
    target_include_directories(wisteria_unit_tests PRIVATE
        ${WISTERIA_BULLET_ROOT}/src
    )
    if(WISTERIA_BUILD_NATIVE)
        target_link_libraries(wisteria_integration_tests PRIVATE wisteria_native)
        target_compile_definitions(wisteria_integration_tests PRIVATE
            WISTERIA_TEST_NATIVE_ABI=1
        )
    endif()
    if(WISTERIA_TEST_FULL_ASSETS)
        foreach(TIER unit runtime integration)
            target_compile_definitions(wisteria_${TIER}_tests PRIVATE
                WISTERIA_TEST_FULL_ASSETS=1
            )
        endforeach()
    endif()

    # ABI safety gate: required, so a machine without Python cannot silently
    # skip the check and let an unguarded export through CI.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME wisteria.abi-safety-matrix
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/script/gen_abi_safety_matrix.py
            --check
    )

    # Minimal render regression: creates a real GL context and verifies that
    # the engine SceneFramebuffer clears and reads back two consecutive
    # frames. Skips when no display backend is available (Linux NULL build).
    add_executable(wisteria_render_tests tests/render_fbo_tests.cpp)
    target_link_libraries(wisteria_render_tests PRIVATE
        wisteria_core
        glfw
    )
    target_compile_definitions(wisteria_render_tests PRIVATE
        GLFW_INCLUDE_NONE
        WISTERIA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    )
    if(MSVC)
        target_compile_options(wisteria_render_tests PRIVATE /utf-8)
    endif()
    add_test(NAME wisteria.render-fbo COMMAND wisteria_render_tests)
    set_tests_properties(wisteria.render-fbo PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()
