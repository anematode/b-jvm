cmake_minimum_required(VERSION 3.15...3.31)
project(bjvm VERSION 0.1
        DESCRIPTION "Portable JVM implementation"
        LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)

list(APPEND CMAKE_MODULE_PATH ${bjvm_SOURCE_DIR}/cmake)

include(CheckIPOSupported)
include(Catch2)
include(Zlib)
include(CTest)

FetchContent_MakeAvailable(Catch2)
if (NOT EMSCRIPTEN)
    FetchContent_MakeAvailable(zlib)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND APPLE AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif ()

add_subdirectory(vendor)
add_subdirectory(src)
add_subdirectory(natives)
add_subdirectory(codegen)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()

add_library(bjvm SHARED)
target_link_libraries(bjvm PUBLIC bjvm_prebuild
        PRIVATE natives)

check_ipo_supported(RESULT HAVE_LTO OUTPUT error)
if (HAVE_LTO)
    message(STATUS "IPO / LTO enabled")
    set_property(TARGET bjvm PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else ()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif ()

if (EMSCRIPTEN)
    add_executable(bjvm_main src/bin/main.c)  # for now
    # add bjvm with -Wl,--whole-archive
    target_link_libraries(bjvm_main PRIVATE bjvm)
    set(EmscriptenFlags "-s EXPORTED_FUNCTIONS=\"['_malloc','_free','_main']\" -sSTACK_SIZE=500000 -Wl,--whole-archive -sWASMFS -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 --emit-tsd bjvm_main.d.ts -s ALLOW_TABLE_GROWTH=1 -s EXPORTED_RUNTIME_METHODS='\"ccall\",\"wasmMemory\",\"cwrap\",\"addFunction\",\"removeFunction\",\"wasmTable\",\"FS\",\"UTF8ToString\",\"getValue\",\"setValue\"'")
    add_link_options("${EmscriptenFlags} -sMODULARIZE -sEXPORT_ES6=1")
endif ()