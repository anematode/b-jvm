cmake_minimum_required(VERSION 3.15...3.31)
project(bjvm VERSION 0.1
            DESCRIPTION "Portable JVM implementation"
            LANGUAGES C CXX)

enable_testing()

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)

list(APPEND CMAKE_MODULE_PATH ${bjvm_SOURCE_DIR}/cmake)

include(probes)
Include(FetchContent)
Set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
        GIT_PROGRESS TRUE
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(Catch2)

if (NOT EMSCRIPTEN)
    FetchContent_Declare(
            zlib
            GIT_REPOSITORY https://github.com/madler/zlib.git
            GIT_TAG v1.3.1
            GIT_PROGRESS TRUE
            GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(zlib)
endif ()

# If doing emscripten
set(BASE_C_FLAGS "-Werror=sign-compare -Wall -Werror -Werror=uninitialized -fms-extensions -Wno-pragmas -Wno-unused-parameter -fno-strict-aliasing -Wno-missing-field-initializers -Wno-format-zero-length")
set(BASE_CXX_FLAGS "-Werror=sign-compare -Wall -Werror=uninitialized -Wno-c99-extensions -Wno-extra-semi -Wno-format-zero-length")

if (EMSCRIPTEN)
    set(PLATFORM_C_FLAGS "-Wno-limited-postlink-optimizations -sUSE_ZLIB=1 -mtail-call")
    set(EmscriptenFlags "-s EXPORTED_FUNCTIONS=\"['_malloc','_free','_main']\" -sSTACK_SIZE=500000 -Wl,--whole-archive -sWASMFS -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 --emit-tsd bjvm_main.d.ts -s ALLOW_TABLE_GROWTH=1 -s EXPORTED_RUNTIME_METHODS='\"ccall\",\"wasmMemory\",\"cwrap\",\"addFunction\",\"removeFunction\",\"wasmTable\",\"FS\",\"UTF8ToString\",\"getValue\",\"setValue\"'")
elseif (APPLE)
    set(PLATFORM_CXX_FLAGS "-Wimplicit-fallthrough -Wno-missing-field-initializers") # -openmp -Xclang -fopenmp -L/opt/homebrew/opt/libomp/lib -I/opt/homebrew/opt/libomp/include -lomp -g")
else ()
    set(PLATFORM_C_FLAGS "-march=native -fPIC")
    set(PLATFORM_CXX_FLAGS "-fopenmp -march=native -fno-strict-aliasing -fPIC")
endif ()

set(CMAKE_C_FLAGS "${BASE_C_FLAGS} ${PLATFORM_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${BASE_CXX_FLAGS} ${PLATFORM_CXX_FLAGS}")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -flto -fomit-frame-pointer -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fomit-frame-pointer -DNDEBUG")

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND APPLE AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
endif ()

add_subdirectory(vendor)
add_subdirectory(src)
add_subdirectory(natives)
add_subdirectory(codegen)
add_subdirectory(test)

add_library(bjvm SHARED)
target_link_libraries(bjvm PUBLIC bjvm_prebuild
                            PRIVATE natives)

if (EMSCRIPTEN)
    add_executable(bjvm_main src/bin/main.c)  # for now
    # add bjvm with -Wl,--whole-archive
    target_link_libraries(bjvm_main PRIVATE bjvm)
    set_target_properties(bjvm_main PROPERTIES LINK_FLAGS "${EmscriptenFlags} -sMODULARIZE -sEXPORT_ES6=1")
endif ()