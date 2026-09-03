cmake_minimum_required(VERSION 3.16)
project(cansdk LANGUAGES CXX VERSION 0.1.0)

# A small, portable CAN diagnostics SDK: frame types and channel interfaces,
# ISO-TP (ISO 15765-2), a UDS/KWP client, SecurityAccess, an ELM327 driver over
# an abstract byte stream, and directioned frame tracing. No platform code — the
# host supplies the transport (a J2534 IChannel, an IByteStream serial/BLE pipe).

add_library(cansdk STATIC
    src/can_frame.cpp
    src/units.cpp
    src/isotp.cpp
    src/uds_client.cpp
    src/security_access.cpp
    src/elm327_channel.cpp
    src/frame_trace.cpp
)
add_library(cansdk::cansdk ALIAS cansdk)

target_include_directories(cansdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(cansdk PUBLIC cxx_std_20)

# Standalone builds may want the tests; a parent project (add_subdirectory) runs
# its own suite, so only offer them at the top level.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(CANSDK_BUILD_TESTS "Build the cansdk test suite" OFF)
endif()
