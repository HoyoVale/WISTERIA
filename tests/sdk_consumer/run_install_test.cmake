# Installed-SDK CTest driver.
#
# Invoked by CTest with:
#   cmake
#     -DWISTERIA_BINARY_DIR=<engine build dir>
#     -DWISTERIA_CONFIG=<config or empty>
#     -DWISTERIA_GENERATOR=<generator>
#     -DWISTERIA_SOURCE_DIR=<engine source dir>
#     -P tests/sdk_consumer/run_install_test.cmake
#
# It performs the full consumer path against an isolated install prefix:
#   install wisteria-sdk -> configure consumer -> build consumer -> run it.

foreach(_required WISTERIA_BINARY_DIR WISTERIA_SOURCE_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(_prefix "${WISTERIA_BINARY_DIR}/sdk-install-test")
set(_consumer_build "${WISTERIA_BINARY_DIR}/sdk-consumer-test")
set(_cpp_consumer_build "${WISTERIA_BINARY_DIR}/sdk-cpp-consumer-test")
file(REMOVE_RECURSE "${_prefix}")
file(REMOVE_RECURSE "${_consumer_build}")
file(REMOVE_RECURSE "${_cpp_consumer_build}")

set(_install_cmd
    "${CMAKE_COMMAND}"
    --install "${WISTERIA_BINARY_DIR}"
    --prefix "${_prefix}"
    --component wisteria-sdk
)
if(WISTERIA_CONFIG)
    list(APPEND _install_cmd --config "${WISTERIA_CONFIG}")
endif()
execute_process(
    COMMAND ${_install_cmd}
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_output
    ERROR_VARIABLE _install_error
)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "SDK install failed (${_install_result})\n${_install_output}${_install_error}")
endif()

set(_configure_cmd
    "${CMAKE_COMMAND}"
    -S "${WISTERIA_SOURCE_DIR}/tests/sdk_consumer"
    -B "${_consumer_build}"
    "-DCMAKE_PREFIX_PATH=${_prefix}"
)
if(WISTERIA_GENERATOR)
    list(APPEND _configure_cmd -G "${WISTERIA_GENERATOR}")
endif()
execute_process(
    COMMAND ${_configure_cmd}
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_output
    ERROR_VARIABLE _configure_error
)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR "SDK consumer configure failed (${_configure_result})\n${_configure_output}${_configure_error}")
endif()

set(_build_cmd
    "${CMAKE_COMMAND}"
    --build "${_consumer_build}"
    --parallel
)
if(WISTERIA_CONFIG)
    list(APPEND _build_cmd --config "${WISTERIA_CONFIG}")
endif()
execute_process(
    COMMAND ${_build_cmd}
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE _build_error
)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "SDK consumer build failed (${_build_result})\n${_build_output}${_build_error}")
endif()

set(_consumer_candidates
    "${_consumer_build}/${WISTERIA_CONFIG}/wisteria_sdk_consumer.exe"
    "${_consumer_build}/wisteria_sdk_consumer.exe"
    "${_consumer_build}/${WISTERIA_CONFIG}/wisteria_sdk_consumer"
    "${_consumer_build}/wisteria_sdk_consumer"
)
set(_consumer "")
foreach(_candidate IN LISTS _consumer_candidates)
    if(EXISTS "${_candidate}")
        set(_consumer "${_candidate}")
        break()
    endif()
endforeach()
if(NOT _consumer)
    message(FATAL_ERROR "SDK consumer executable was not found")
endif()

# Make the installed shared library discoverable when running the consumer.
if(WIN32)
    set(ENV{PATH} "${_prefix}/bin;$ENV{PATH}")
else()
    set(ENV{LD_LIBRARY_PATH} "${_prefix}/lib:$ENV{LD_LIBRARY_PATH}")
endif()

execute_process(
    COMMAND "${_consumer}"
    RESULT_VARIABLE _consumer_result
    OUTPUT_VARIABLE _consumer_output
    ERROR_VARIABLE _consumer_error
)
if(NOT _consumer_result EQUAL 0)
    message(FATAL_ERROR "SDK consumer run failed (${_consumer_result})\n${_consumer_output}${_consumer_error}")
endif()

set(_cpp_configure_cmd
    "${CMAKE_COMMAND}"
    -S "${WISTERIA_SOURCE_DIR}/tests/cpp_sdk_consumer"
    -B "${_cpp_consumer_build}"
    "-DCMAKE_PREFIX_PATH=${_prefix}"
)
if(WISTERIA_GENERATOR)
    list(APPEND _cpp_configure_cmd -G "${WISTERIA_GENERATOR}")
endif()
execute_process(
    COMMAND ${_cpp_configure_cmd}
    RESULT_VARIABLE _cpp_configure_result
    OUTPUT_VARIABLE _cpp_configure_output
    ERROR_VARIABLE _cpp_configure_error
)
if(NOT _cpp_configure_result EQUAL 0)
    message(FATAL_ERROR "SDK C++ consumer configure failed (${_cpp_configure_result})\n${_cpp_configure_output}${_cpp_configure_error}")
endif()

set(_cpp_build_cmd
    "${CMAKE_COMMAND}"
    --build "${_cpp_consumer_build}"
    --parallel
)
if(WISTERIA_CONFIG)
    list(APPEND _cpp_build_cmd --config "${WISTERIA_CONFIG}")
endif()
execute_process(
    COMMAND ${_cpp_build_cmd}
    RESULT_VARIABLE _cpp_build_result
    OUTPUT_VARIABLE _cpp_build_output
    ERROR_VARIABLE _cpp_build_error
)
if(NOT _cpp_build_result EQUAL 0)
    message(FATAL_ERROR "SDK C++ consumer build failed (${_cpp_build_result})\n${_cpp_build_output}${_cpp_build_error}")
endif()

set(_cpp_consumer_candidates
    "${_cpp_consumer_build}/${WISTERIA_CONFIG}/wisteria_cpp_sdk_consumer.exe"
    "${_cpp_consumer_build}/wisteria_cpp_sdk_consumer.exe"
    "${_cpp_consumer_build}/${WISTERIA_CONFIG}/wisteria_cpp_sdk_consumer"
    "${_cpp_consumer_build}/wisteria_cpp_sdk_consumer"
)
set(_cpp_consumer "")
foreach(_candidate IN LISTS _cpp_consumer_candidates)
    if(EXISTS "${_candidate}")
        set(_cpp_consumer "${_candidate}")
        break()
    endif()
endforeach()
if(NOT _cpp_consumer)
    message(FATAL_ERROR "SDK C++ consumer executable was not found")
endif()

if(WIN32)
    # GitHub Windows runners have no OpenGL 3.3 headless provider. The C++
    # consumer still verifies context/entity/checkpoint; only the optional
    # render step is skipped there. Linux remains strict.
    set(ENV{WISTERIA_SDK_ALLOW_RENDER_SKIP} "1")
endif()
execute_process(
    COMMAND "${_cpp_consumer}"
        "${WISTERIA_SOURCE_DIR}/tests/data/animated_triangle.gltf"
        render
    RESULT_VARIABLE _cpp_consumer_result
    OUTPUT_VARIABLE _cpp_consumer_output
    ERROR_VARIABLE _cpp_consumer_error
)
if(NOT _cpp_consumer_result EQUAL 0)
    message(FATAL_ERROR "SDK C++ consumer run failed (${_cpp_consumer_result})\n${_cpp_consumer_output}${_cpp_consumer_error}")
endif()

message(STATUS "SDK install C++ consumer passed: ${_cpp_consumer_output}")
message(STATUS "SDK install consumer passed: ${_consumer_output}")
