cmake_minimum_required(VERSION 3.28)
project(bjvm)

enable_testing()

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 17)
Include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(Catch2)

if (NOT EMSCRIPTEN)
FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
)

FetchContent_MakeAvailable(zlib)
endif()

# If doing emscripten
if (EMSCRIPTEN)
    set(CMAKE_C_FLAGS "-O2 -Wall -Wextra -Wpedantic -sUSE_ZLIB=1")
    set(CMAKE_CXX_FLAGS "-O2 -Wall -Wpedantic")
    set(EmscriptenFlags "-O2 -s EXPORTED_FUNCTIONS=\"['_malloc','_free','_main']\" -gsource-map -sSTACK_SIZE=500000 -sALLOW_MEMORY_GROWTH=1 -sALLOW_TABLE_GROWTH=1 -s EXPORTED_RUNTIME_METHODS='\"ccall\",\"wasmMemory\",\"cwrap\",\"addFunction\",\"removeFunction\",\"wasmTable\"'")
elseif (APPLE)
    set(CMAKE_C_FLAGS "-O3 -Wall -Wextra -Wimplicit-fallthrough -g -Werror=incompatible-pointer-types -Wno-missing-field-initializers -fno-strict-aliasing")
    set(CMAKE_CXX_FLAGS "-O3 -Wall -Wextra -Wimplicit-fallthrough -g -Werror=incompatible-pointer-types -Wno-missing-field-initializers -fno-strict-aliasing") # -openmp -Xclang -fopenmp -L/opt/homebrew/opt/libomp/lib -I/opt/homebrew/opt/libomp/include -lomp -g")
    set(EmscriptenFlags "")
else ()
    set(CMAKE_C_FLAGS "-O3 -Wall -Wextra -g -fsanitize=address,undefined -march=native -fno-strict-aliasing -lm")
    set(CMAKE_CXX_FLAGS "-O3 -Wall -g -fsanitize=address,undefined -fopenmp -march=native -fno-strict-aliasing -lm")
    set(EmscriptenFlags "")
endif ()

if (TRUE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG")
endif ()

if (FALSE) # NOT EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
endif ()

file(GLOB bjvm_SRC CONFIGURE_DEPENDS "src/*.c" "src/*.h")
file(GLOB_RECURSE bjvm_NATIVES CONFIGURE_DEPENDS "src/natives/*.c" "src/natives/*.h")

add_library(bjvm OBJECT ${bjvm_SRC} ${bjvm_NATIVES}
        src/arm64_jit.c
        src/arm64_jit.h
        src/natives/java/util/TimeZone.c
        src/vtable.c
        src/vtable.h
        src/ir.c
        src/ir.h)
target_include_directories(bjvm PRIVATE src)

if (NOT EMSCRIPTEN)
target_link_libraries(bjvm PRIVATE zlibstatic)
else()
target_link_libraries(bjvm PRIVATE z)
endif()

if (EMSCRIPTEN)
    add_executable(bjvm_main src/bin/main.cc test/tests-common.cc)  # for now
    target_link_libraries(bjvm_main PRIVATE bjvm)
    set_target_properties(bjvm_main PROPERTIES LINK_FLAGS "${EmscriptenFlags}")
endif()

if (NOT APPLE)
    set_target_properties(bjvm PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/natives_table.ld)
endif ()

add_executable(gen_natives codegen/gen_natives.c)
target_link_libraries(gen_natives PRIVATE bjvm)
set_target_properties(gen_natives PROPERTIES LINK_FLAGS "${EmscriptenFlags}")

add_executable(tests test/tests.cc test/arrays-test.cc test/tests-common.cc test/wasm-tests.cc
        test/analysis-tests.cc
        test/classpath-tests.cc
        test/benches.cc)
target_link_libraries(tests PRIVATE bjvm)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
set_target_properties(tests PROPERTIES LINK_FLAGS "${EmscriptenFlags}")

add_test(NAME tests
        COMMAND tests --skip-benchmarks
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test")
set_tests_properties(tests PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1,detect_container_overflow=0")