cmake_minimum_required(VERSION 3.20)

# Installed C++ SDK consumer test. Configure with:
#   cmake -S tests/cpp_sdk_consumer -B <build> -DCMAKE_PREFIX_PATH=<install-prefix>
#
# It links Wisteria::cpp (header-only RAII wrappers) and Wisteria::native.
project(WisteriaCppSdkConsumer LANGUAGES CXX)

find_package(Wisteria 1.2 CONFIG REQUIRED)

add_executable(wisteria_cpp_sdk_consumer main.cpp)
target_link_libraries(wisteria_cpp_sdk_consumer PRIVATE Wisteria::cpp)
target_compile_features(wisteria_cpp_sdk_consumer PRIVATE cxx_std_20)
