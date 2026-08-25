cmake_minimum_required(VERSION 3.20)

# Installed-SDK consumer test. Configure with:
#   cmake -S tests/sdk_consumer -B <build> -DCMAKE_PREFIX_PATH=<install-prefix>
#
# It intentionally uses only the C language and the stable C ABI headers.
project(WisteriaSdkConsumer LANGUAGES C)

find_package(Wisteria 1.1 CONFIG REQUIRED)

add_executable(wisteria_sdk_consumer main.c)
target_link_libraries(wisteria_sdk_consumer PRIVATE Wisteria::native)
