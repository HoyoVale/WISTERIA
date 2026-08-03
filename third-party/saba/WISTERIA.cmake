# WISTERIA integration for the Saba MMD runtime.
#
# Saba is MIT licensed (see third-party/saba/LICENCE) and is vendored as the
# reference implementation for the MMD adapter rewrite. This wrapper builds
# only the core Base + MMD runtime library. Saba's own CMake (viewer, gtests,
# examples, OBJ/XFile model loaders) is intentionally not used; WISTERIA has
# its own Assimp-based importers.

set(WISTERIA_SABA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third-party/saba")

if(NOT EXISTS "${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDPhysics.cpp")
    message(FATAL_ERROR
        "Saba is missing. Clone it into third-party/saba before configuring.")
endif()

set(WISTERIA_SABA_SOURCES
    ${WISTERIA_SABA_ROOT}/src/Saba/Base/File.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Base/Log.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Base/Path.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Base/Singleton.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Base/Time.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Base/UnicodeUtil.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDCamera.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDIkSolver.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDMaterial.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDModel.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDMorph.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDNode.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/MMDPhysics.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/PMDFile.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/PMDModel.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/PMXFile.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/PMXModel.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/SjisToUnicode.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/VMDAnimation.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/VMDCameraAnimation.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/VMDFile.cpp
    ${WISTERIA_SABA_ROOT}/src/Saba/Model/MMD/VPDFile.cpp
)

add_library(saba STATIC ${WISTERIA_SABA_SOURCES})

target_include_directories(saba
    PUBLIC
        ${WISTERIA_SABA_ROOT}/src
        ${WISTERIA_SABA_ROOT}/external/glm/include
        ${WISTERIA_SABA_ROOT}/external/spdlog/include
    PRIVATE
        ${WISTERIA_BULLET_ROOT}/src
)

target_compile_features(saba PRIVATE cxx_std_14)

if(WIN32)
    # Saba's own CMake defines these on Windows; Path.cpp mixes
    # GetCurrentDirectoryW with the GetCurrentDirectory macro.
    target_compile_definitions(saba PRIVATE UNICODE _UNICODE)
endif()

target_link_libraries(saba PRIVATE
    BulletDynamics
    BulletCollision
    LinearMath
)

if(MSVC)
    target_compile_options(saba PRIVATE /utf-8 /FS)
    set_property(TARGET saba PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
