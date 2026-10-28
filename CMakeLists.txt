cmake_minimum_required(VERSION 3.16)
project(binpack VERSION 2.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(BINPACK_BUILD_CLI "Build standalone command-line executable" ON)
option(BINPACK_BUILD_STATIC "Build static library (libbinpack.a / binpack.lib)" ON)
option(BINPACK_BUILD_SHARED "Build shared library (libbinpack.so / binpack.dll)" ON)
option(BINPACK_BUILD_TESTS "Build native benchmark & test suite" ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} deps/include)

# Find Zstandard library
if(WIN32)
    find_library(ZSTD_LIBRARY NAMES libzstd_static zstd libzstd PATHS ${CMAKE_CURRENT_SOURCE_DIR}/deps/lib)
else()
    find_library(ZSTD_LIBRARY NAMES zstd)
    if(NOT ZSTD_LIBRARY)
        set(ZSTD_LIBRARY "-lzstd")
    endif()
endif()

# Compiler optimization flags
if(MSVC)
    add_compile_options(/O2 /W3)
else()
    add_compile_options(-O3 -Wall -Wextra)
endif()

# Static library
if(BINPACK_BUILD_STATIC)
    add_library(binpack_static STATIC binpack.c)
    target_link_libraries(binpack_static PRIVATE ${ZSTD_LIBRARY})
    set_target_properties(binpack_static PROPERTIES OUTPUT_NAME "binpack")
endif()

# Shared library
if(BINPACK_BUILD_SHARED)
    add_library(binpack_shared SHARED binpack.c)
    target_link_libraries(binpack_shared PRIVATE ${ZSTD_LIBRARY})
    set_target_properties(binpack_shared PROPERTIES OUTPUT_NAME "binpack")
endif()

# Standalone CLI tool
if(BINPACK_BUILD_CLI)
    add_executable(binpack_cli binpack.c binpack_cli.c)
    target_link_libraries(binpack_cli PRIVATE ${ZSTD_LIBRARY})
    set_target_properties(binpack_cli PROPERTIES OUTPUT_NAME "binpack")
endif()

# Native test & benchmark suite
if(BINPACK_BUILD_TESTS)
    add_executable(test_native binpack.c test_native.c)
    target_link_libraries(test_native PRIVATE ${ZSTD_LIBRARY})
endif()
