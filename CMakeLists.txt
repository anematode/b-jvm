cmake_minimum_required(VERSION 3.25)
project(bjvm)

enable_testing()

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 20)

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
set(BASE_C_FLAGS "-g -O2 -Werror=sign-compare -Wall -Werror -Werror=uninitialized -foptimize-sibling-calls -fms-extensions -Wno-pragmas -Wno-unused-parameter -fno-strict-aliasing -Wno-missing-field-initializers -Wno-format-zero-length")
set(BASE_CXX_FLAGS "-g -O2 -Werror=sign-compare -Wall -Werror=uninitialized -Wno-c99-extensions -Wno-extra-semi -Wno-format-zero-length")

if (EMSCRIPTEN)
    set(PLATFORM_C_FLAGS "-sUSE_ZLIB=1 -mtail-call")
    set(EmscriptenFlags "-s EXPORTED_FUNCTIONS=\"['_malloc','_free','_main']\" -sSTACK_SIZE=500000 -Wl,--whole-archive -sWASMFS -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 --emit-tsd bjvm_main.d.ts -s ALLOW_TABLE_GROWTH=1 -s EXPORTED_RUNTIME_METHODS='\"ccall\",\"wasmMemory\",\"cwrap\",\"addFunction\",\"removeFunction\",\"wasmTable\",\"FS\",\"UTF8ToString\",\"getValue\",\"setValue\"'")
elseif (APPLE)
    set(PLATFORM_CXX_FLAGS "-Wimplicit-fallthrough -Wno-missing-field-initializers") # -openmp -Xclang -fopenmp -L/opt/homebrew/opt/libomp/lib -I/opt/homebrew/opt/libomp/include -lomp -g")
else ()
    set(PLATFORM_C_FLAGS "-march=native -fPIC")
    set(PLATFORM_CXX_FLAGS "-fopenmp -march=native -fno-strict-aliasing -fPIC")
endif ()

set(CMAKE_C_FLAGS "${BASE_C_FLAGS} ${PLATFORM_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${BASE_CXX_FLAGS} ${PLATFORM_CXX_FLAGS}")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -fomit-frame-pointer -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto -fomit-frame-pointer -DNDEBUG")

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND APPLE AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
    set(DTRACE_SUPPORT 1)
endif ()

file(GLOB bjvm_SRC CONFIGURE_DEPENDS "src/*.c" "src/*.h")
file(GLOB_RECURSE bjvm_NATIVES CONFIGURE_DEPENDS "src/natives/share/*.c" "src/natives/share/*.h")
file(GLOB bjvm_wasm_SRC CONFIGURE_DEPENDS "src/wasm/*.c" "src/wasm/*.h")

if (UNIX)
    file(GLOB_RECURSE bjvm_posix_NATIVES CONFIGURE_DEPENDS "src/natives/posix/*.c" "src/natives/posix/*.h")
    list(APPEND bjvm_SRC ${bjvm_posix_NATIVES})
endif ()

if (EMSCRIPTEN)
    list(APPEND bjvm_SRC ${bjvm_wasm_SRC})
endif ()

add_library(bjvm_prebuild OBJECT ${bjvm_SRC} ${bjvm_NATIVES}
        vendor/stb_ds.h
        vendor/stb_ds.c
)
target_include_directories(bjvm_prebuild PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
set(BJVM_LINK_OBJECTS "$<TARGET_OBJECTS:bjvm_prebuild>")

if (DTRACE_SUPPORT)
    add_dtrace_support(bjvm_prebuild "${CMAKE_CURRENT_SOURCE_DIR}/src/probes.d")
endif ()

add_library(bjvm SHARED ${BJVM_LINK_OBJECTS})
target_link_libraries(bjvm PUBLIC bjvm_prebuild)

if (NOT EMSCRIPTEN)
    target_link_libraries(bjvm PRIVATE z)
endif ()

target_link_libraries(bjvm PRIVATE m)

add_executable(bjvm_async_test src/bin/async.c)  # for now
target_link_libraries(bjvm_async_test PRIVATE bjvm)

if (EMSCRIPTEN)
    add_executable(bjvm_main src/bin/main.c)  # for now
    # add bjvm with -Wl,--whole-archive
    target_link_libraries(bjvm_main PRIVATE bjvm)
    set_target_properties(bjvm_main PROPERTIES LINK_FLAGS "${EmscriptenFlags} -sMODULARIZE -sEXPORT_ES6=1")
endif ()

add_executable(gen_natives codegen/gen_natives.c)
target_link_libraries(gen_natives PRIVATE bjvm)

add_executable(tests test/tests.cc test/arrays-test.cc test/tests-common.cc test/wasm-tests.cc
        test/async-test.cc
        test/analysis-tests.cc
        test/classpath-tests.cc
        test/natives-test.cc
        test/benches.cc)
if (NOT EMSCRIPTEN)
    target_sources(tests PRIVATE ${bjvm_wasm_SRC})
endif ()
target_link_libraries(tests PRIVATE z)
target_link_libraries(tests PRIVATE bjvm)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
set_target_properties(tests PROPERTIES LINK_FLAGS "${EmscriptenFlags}")

add_test(NAME tests
        COMMAND tests --skip-benchmarks
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test")
set_tests_properties(tests PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1,detect_container_overflow=0")