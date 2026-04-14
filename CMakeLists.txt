cmake_minimum_required(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(lu-assets
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "LEGO Universe asset format library"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Guard: only define options/tests when this is the top-level project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(LU_ASSETS_IS_TOP_LEVEL ON)
else()
    set(LU_ASSETS_IS_TOP_LEVEL OFF)
endif()

option(LU_ASSETS_BUILD_TESTS "Build unit tests" ${LU_ASSETS_IS_TOP_LEVEL})

# FetchContent for dependencies
include(FetchContent)

# zlib - required for SD0 decompression
# Try system first, fall back to FetchContent (Windows CI)
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    FetchContent_Declare(zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
    )
    FetchContent_MakeAvailable(zlib)
    # zlib's CMake doesn't export ZLIB::ZLIB, create alias
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
endif()

# SQLite3 - for CDClient database
# Try system first, fall back to building from amalgamation
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    FetchContent_Declare(sqlite3
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    )
    FetchContent_MakeAvailable(sqlite3)
    add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
    if(NOT TARGET SQLite::SQLite3)
        add_library(SQLite::SQLite3 ALIAS sqlite3_lib)
    endif()
endif()

# pugixml - for LXFML and other XML parsing
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.14
)
FetchContent_MakeAvailable(pugixml)

# stb - single-header libraries (stb_image for TGA loading)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# Needed before add_subdirectory(src) for INSTALL_INTERFACE generator expressions
include(GNUInstallDirs)

# Core asset library
add_subdirectory(src)

# Alias target for FetchContent consumers
add_library(lu_assets::lu_assets ALIAS lu_assets)

# Tests
if(LU_ASSETS_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
endif()

# CMake package config (install targets) — only when building standalone
# with system-installed deps. FetchContent deps (zlibstatic, sqlite3_lib)
# can't be exported, so skip install when they're in use.
if(LU_ASSETS_IS_TOP_LEVEL AND ZLIB_FOUND AND SQLite3_FOUND)
    include(CMakePackageConfigHelpers)

    install(TARGETS lu_assets
        EXPORT lu_assets-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lu_assets
    )

    install(DIRECTORY src/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lu_assets
        FILES_MATCHING PATTERN "*.h"
    )

    install(EXPORT lu_assets-targets
        FILE lu_assets-targets.cmake
        NAMESPACE lu_assets::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lu_assets
    )

    configure_package_config_file(
        cmake/lu_assets-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/lu_assets-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lu_assets
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/lu_assets-config-version.cmake
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/lu_assets-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/lu_assets-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lu_assets
    )
endif()
