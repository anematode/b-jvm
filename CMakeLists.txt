cmake_minimum_required(VERSION 3.15...3.31)
project(bjvm VERSION 0.1
        DESCRIPTION "Portable JVM implementation"
        LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)

list(APPEND CMAKE_MODULE_PATH ${bjvm_SOURCE_DIR}/cmake)

include(Catch2)
include(CTest)
include(AsanSupport)
include(LtoSupport)

FetchContent_MakeAvailable(Catch2)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_subdirectory(vendor)
add_subdirectory(vm)
add_subdirectory(natives)
add_subdirectory(codegen)

if (BUILD_TESTING)
    add_subdirectory(test)
endif ()

add_library(bjvm_static STATIC)
target_link_libraries(bjvm_static PUBLIC vm PRIVATE natives)

if (NOT EMSCRIPTEN)
    add_library(bjvm SHARED)
    target_link_libraries(bjvm PUBLIC vm PRIVATE natives)
endif()

if (EMSCRIPTEN)
    target_link_options(bjvm_static PUBLIC -sEXPORTED_FUNCTIONS=_malloc,_free,_main
            -sSTACK_SIZE=500000
            -sWASMFS
            -sFORCE_FILESYSTEM=1
            -sALLOW_MEMORY_GROWTH=1 -sALLOW_TABLE_GROWTH=1
            -sEXPORTED_RUNTIME_METHODS=ccall,wasmMemory,cwrap,addFunction,removeFunction,wasmTable,FS,UTF8ToString,getValue,setValue
            )

    add_executable(bjvm_main vm/bin/main.c)  # for now
    # add bjvm with -Wl,--whole-archive
    target_link_libraries(bjvm_main PRIVATE bjvm_static)
    add_link_options(--emit-tsd bjvm_main.d.ts -sMODULARIZE -sEXPORT_ES6=1)
endif ()