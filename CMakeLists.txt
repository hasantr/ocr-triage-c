cmake_minimum_required(VERSION 3.15)
project(ocr_triage
    VERSION 0.1.0
    DESCRIPTION "Sub-millisecond image-has-text classifier"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -ffast-math)
endif()

option(OCR_TRIAGE_BUILD_SHARED "Build shared library" ON)
option(OCR_TRIAGE_BUILD_TESTS  "Build tests"           ON)
option(OCR_TRIAGE_BUILD_BENCH  "Build benchmarks"      ON)
option(OCR_TRIAGE_BUILD_CLI    "Build triage_cli demo" ON)

set(OCR_TRIAGE_SOURCES
    src/triage.c
    src/thumbnail.c
    src/score.c
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

if(NOT MSVC)
    target_link_libraries(ocr_triage PRIVATE m)
endif()

set_target_properties(ocr_triage PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER include/ocr_triage.h
)

# -------- Tests --------
if(OCR_TRIAGE_BUILD_TESTS)
    enable_testing()
    add_executable(test_basic tests/test_basic.c)
    target_link_libraries(test_basic PRIVATE ocr_triage)
    add_test(NAME basic COMMAND test_basic)
endif()

# -------- CLI demo --------
if(OCR_TRIAGE_BUILD_CLI)
    add_executable(triage_cli examples/triage_cli.c)
    target_link_libraries(triage_cli PRIVATE ocr_triage)
endif()

# -------- Bench --------
if(OCR_TRIAGE_BUILD_BENCH)
    add_executable(bench_raw bench/bench_raw.c)
    target_link_libraries(bench_raw PRIVATE ocr_triage)
endif()

# -------- Install --------
include(GNUInstallDirs)
install(TARGETS ocr_triage
    EXPORT ocr_triage-targets
    LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
