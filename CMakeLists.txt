cmake_minimum_required(VERSION 3.20)
project(Kestrel
    VERSION 1.4.0.0
    LANGUAGES CXX C
    DESCRIPTION "Kestrel - Battery, system and frame-time overlay for Windows"
)

# Options
option(KESTREL_BUILD_TESTS "Build unit tests" OFF)

# Policies
cmake_policy(SET CMP0077 NEW)  # option() honors normal variables
cmake_policy(SET CMP0091 NEW)  # WIN32_EXECUTABLE sets WIN32 on target

# Minimum C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type (default to Release if not specified)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Static linking flags
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -DNDEBUG")
endif()

# Precompiled header
set(PCH_HEADER "include/pch.h")
set(PCH_SOURCE "src/pch.cpp")

# Create precompiled header source file if it doesn't exist
configure_file(${PCH_HEADER}.in ${PCH_HEADER} @ONLY)

# Source files
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

# Resource file
set(RC_FILE "app.rc")
set(RC_MANIFEST "app.manifest")
set(ICON_FILE "assets/kestrel.ico")

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Windows libraries
set(WIN_LIBS
    wbemuuid
    ole32
    oleaut32
    oleacc
    uuid
    gdi32
    shell32
    setupapi
    d2d1
    dwrite
    dwmapi
    pdh
    iphlpapi
    advapi32
    winpthread
    psapi
    taskschd
    shlwapi
    user32
    comctl32
    winhttp
    version
    dxgi
)

# Executable
add_executable(kestrel WIN32 ${SRC_FILES})
target_link_libraries(kestrel PRIVATE ${WIN_LIBS})

# Compile options
target_compile_options(kestrel PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -MMD
    -MP
)

# Precompiled header - CMake handles PCH automatically
target_precompile_headers(kestrel PRIVATE
    <windows.h>
    <d2d1.h>
    <dwrite.h>
    <pdh.h>
    <vector>
    <string>
    <map>
    <algorithm>
    <memory>
    <cstdint>
    <cstdio>
    <cmath>
)

# Resource compilation with windres (separate from main build)
find_program(WINDRES windres REQUIRED)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/app_rc.o
    COMMAND ${WINDRES}
        -I ${CMAKE_CURRENT_SOURCE_DIR}/include
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/${RC_FILE}
        -O coff
        -o ${CMAKE_CURRENT_BINARY_DIR}/app_rc.o
    DEPENDS ${RC_FILE} ${RC_MANIFEST} ${ICON_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/include/resource_bi.h ${CMAKE_CURRENT_SOURCE_DIR}/include/app_identity_bi.h
    COMMENT "Compiling resources with windres"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_custom_target(kestrel_res ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/app_rc.o)
add_dependencies(kestrel kestrel_res)
target_link_libraries(kestrel PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app_rc.o)

# Output directory
set_target_properties(kestrel PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    DEBUG_POSTFIX ""
)

# Install (optional)
install(TARGETS kestrel
    RUNTIME DESTINATION bin
)

# Custom targets
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/app_rc.o
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/Makefile
    COMMENT "Cleaning build artifacts"
)

# ─── Unit Tests ────────────────────────────────────────────────────────────

if(KESTREL_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.6.0
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()

    file(GLOB_RECURSE TEST_SRC_FILES "tests/*.cpp")

    add_executable(kestrel_tests
        ${TEST_SRC_FILES}
        src/hud_bi.cpp
    )
    target_include_directories(kestrel_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(kestrel_tests PRIVATE Catch2::Catch2WithMain)

    target_compile_options(kestrel_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )

    # Link Windows libs needed by tested code
    target_link_libraries(kestrel_tests PRIVATE
        wbemuuid ole32 oleaut32 uuid setupapi pdh iphlpapi advapi32
        d2d1 dwrite dwmapi shlwapi version dxgi
    )

    add_test(NAME kestrel_tests COMMAND kestrel_tests)
endif()