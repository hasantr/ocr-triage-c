cmake_minimum_required(VERSION 3.24)
project(ocr_triage
    VERSION 0.2.0
    DESCRIPTION "Sub-millisecond image-has-text classifier"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -ffast-math)
endif()

# POSIX clock_gettime / struct timespec visibility on Linux — applies to
# lib + all tests, bench, examples. macOS/BSD expose these by default.
if(UNIX AND NOT APPLE)
    add_compile_definitions(_POSIX_C_SOURCE=199309L)
endif()

# ================================================================
# Options
# ================================================================
option(OCR_TRIAGE_BUILD_SHARED "Build shared library"    ON)
option(OCR_TRIAGE_BUILD_TESTS  "Build tests"             ON)
option(OCR_TRIAGE_BUILD_BENCH  "Build benchmarks"        ON)
option(OCR_TRIAGE_BUILD_CLI    "Build triage_cli demo"   ON)
option(OCR_TRIAGE_WITH_JPEG    "Enable JPEG (libjpeg-turbo)" ON)
option(OCR_TRIAGE_WITH_PNG     "Enable PNG  (libspng + zlib)" ON)

# ================================================================
# Third-party: libjpeg-turbo (ExternalProject — upstream blocks FetchContent)
# ================================================================
if(OCR_TRIAGE_WITH_JPEG)
    include(ExternalProject)
    set(JPEG_PREFIX  ${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo)
    set(JPEG_INSTALL ${JPEG_PREFIX}/install)

    if(MSVC)
        set(JPEG_LIB ${JPEG_INSTALL}/lib/turbojpeg-static.lib)
    else()
        set(JPEG_LIB ${JPEG_INSTALL}/lib/libturbojpeg.a)
    endif()

    # NASM auto-detect for SIMD path (optional).
    find_program(NASM_EXECUTABLE nasm)
    if(NASM_EXECUTABLE)
        set(JPEG_WITH_SIMD ON)
        message(STATUS "NASM found: SIMD enabled for libjpeg-turbo")
    else()
        set(JPEG_WITH_SIMD OFF)
        message(STATUS "NASM not found: libjpeg-turbo built without SIMD (pure C)")
    endif()

    ExternalProject_Add(libjpeg_turbo_ep
        GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
        GIT_TAG        3.1.4
        GIT_SHALLOW    TRUE
        PREFIX         ${JPEG_PREFIX}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${JPEG_INSTALL}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DENABLE_SHARED=OFF
            -DENABLE_STATIC=ON
            -DWITH_SIMD=${JPEG_WITH_SIMD}
            -DWITH_TURBOJPEG=ON
            -DWITH_JAVA=OFF
            -DWITH_JPEG8=OFF
            -DWITH_TOOLS=OFF
            -DWITH_TESTS=OFF
            -DWITH_FUZZ=OFF
        BUILD_BYPRODUCTS ${JPEG_LIB}
        UPDATE_DISCONNECTED TRUE
    )

    file(MAKE_DIRECTORY ${JPEG_INSTALL}/include)

    add_library(turbojpeg_imported STATIC IMPORTED GLOBAL)
    set_target_properties(turbojpeg_imported PROPERTIES
        IMPORTED_LOCATION ${JPEG_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${JPEG_INSTALL}/include
    )
    add_dependencies(turbojpeg_imported libjpeg_turbo_ep)
endif()

# ================================================================
# Third-party: zlib + libspng (FetchContent, add_subdirectory-friendly)
# ================================================================
if(OCR_TRIAGE_WITH_PNG)
    include(ExternalProject)

    # zlib-ng in ZLIB_COMPAT mode — avoids MinGW windres bug in upstream zlib 1.3.1.
    set(ZLIB_PREFIX  ${CMAKE_BINARY_DIR}/_deps/zlib-ng)
    set(ZLIB_INSTALL ${ZLIB_PREFIX}/install)
    if(MSVC)
        set(ZLIB_LIB ${ZLIB_INSTALL}/lib/zlibstatic.lib)
    else()
        set(ZLIB_LIB ${ZLIB_INSTALL}/lib/libz.a)
    endif()

    ExternalProject_Add(zlib_ep
        GIT_REPOSITORY https://github.com/zlib-ng/zlib-ng.git
        GIT_TAG        2.1.6
        GIT_SHALLOW    TRUE
        PREFIX         ${ZLIB_PREFIX}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${ZLIB_INSTALL}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DZLIB_COMPAT=ON
            -DZLIB_ENABLE_TESTS=OFF
            -DZLIBNG_ENABLE_TESTS=OFF
            -DBUILD_SHARED_LIBS=OFF
            -DWITH_GTEST=OFF
        BUILD_BYPRODUCTS ${ZLIB_LIB}
        UPDATE_DISCONNECTED TRUE
    )
    file(MAKE_DIRECTORY ${ZLIB_INSTALL}/include)
    add_library(zlib_imported STATIC IMPORTED GLOBAL)
    set_target_properties(zlib_imported PROPERTIES
        IMPORTED_LOCATION ${ZLIB_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${ZLIB_INSTALL}/include
    )
    add_dependencies(zlib_imported zlib_ep)

    # libspng
    set(SPNG_PREFIX  ${CMAKE_BINARY_DIR}/_deps/libspng)
    set(SPNG_INSTALL ${SPNG_PREFIX}/install)
    if(MSVC)
        set(SPNG_LIB ${SPNG_INSTALL}/lib/spng_static.lib)
    else()
        set(SPNG_LIB ${SPNG_INSTALL}/lib/libspng_static.a)
    endif()

    ExternalProject_Add(libspng_ep
        GIT_REPOSITORY https://github.com/randy408/libspng.git
        GIT_TAG        v0.7.4
        GIT_SHALLOW    TRUE
        PREFIX         ${SPNG_PREFIX}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${SPNG_INSTALL}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DSPNG_SHARED=OFF
            -DSPNG_STATIC=ON
            -DBUILD_EXAMPLES=OFF
            -DZLIB_ROOT=${ZLIB_INSTALL}
            -DZLIB_INCLUDE_DIR=${ZLIB_INSTALL}/include
            -DZLIB_LIBRARY=${ZLIB_LIB}
        BUILD_BYPRODUCTS ${SPNG_LIB}
        DEPENDS zlib_ep
        UPDATE_DISCONNECTED TRUE
    )
    file(MAKE_DIRECTORY ${SPNG_INSTALL}/include)
    add_library(spng_imported STATIC IMPORTED GLOBAL)
    set_target_properties(spng_imported PROPERTIES
        IMPORTED_LOCATION ${SPNG_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${SPNG_INSTALL}/include
        INTERFACE_COMPILE_DEFINITIONS SPNG_STATIC
    )
    add_dependencies(spng_imported libspng_ep)
endif()

# ================================================================
# ocr_triage library
# ================================================================
set(OCR_TRIAGE_SOURCES
    src/triage.c
    src/thumbnail.c
    src/score.c
    src/decode.c
)

if(OCR_TRIAGE_BUILD_SHARED)
    add_library(ocr_triage SHARED ${OCR_TRIAGE_SOURCES})
else()
    add_library(ocr_triage STATIC ${OCR_TRIAGE_SOURCES})
endif()

target_include_directories(ocr_triage
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE src
)

# Auto-export every symbol on MSVC DLL builds so callers don't need
# __declspec(dllexport) annotations on each function.
if(WIN32 AND MSVC AND OCR_TRIAGE_BUILD_SHARED)
    set_target_properties(ocr_triage PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(NOT MSVC)
    target_link_libraries(ocr_triage PRIVATE m)
endif()

if(OCR_TRIAGE_WITH_JPEG)
    target_compile_definitions(ocr_triage PRIVATE OCR_TRIAGE_WITH_JPEG=1)
    target_link_libraries(ocr_triage PRIVATE turbojpeg_imported)
endif()

if(OCR_TRIAGE_WITH_PNG)
    target_compile_definitions(ocr_triage PRIVATE OCR_TRIAGE_WITH_PNG=1)
    target_link_libraries(ocr_triage PRIVATE spng_imported zlib_imported)
endif()

set_target_properties(ocr_triage PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER include/ocr_triage.h
)

# ================================================================
# Tests / CLI / Bench
# ================================================================
if(OCR_TRIAGE_BUILD_TESTS)
    enable_testing()
    add_executable(test_basic tests/test_basic.c)
    target_link_libraries(test_basic PRIVATE ocr_triage)
    add_test(NAME basic COMMAND test_basic)
endif()

if(OCR_TRIAGE_BUILD_CLI)
    add_executable(triage_cli examples/triage_cli.c)
    target_link_libraries(triage_cli PRIVATE ocr_triage)
endif()

if(OCR_TRIAGE_BUILD_BENCH)
    add_executable(bench_raw bench/bench_raw.c)
    target_link_libraries(bench_raw PRIVATE ocr_triage)

    add_executable(bench_encoded bench/bench_encoded.c)
    target_link_libraries(bench_encoded PRIVATE ocr_triage)
endif()

# ================================================================
# Install
# ================================================================
include(GNUInstallDirs)
install(TARGETS ocr_triage
    EXPORT ocr_triage-targets
    LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
