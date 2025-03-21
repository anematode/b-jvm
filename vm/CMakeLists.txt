set(CMAKE_C_STANDARD 23)

include(CheckSymbolExists)
include(probes)
include(Zlib)
include(CheckIncludeFile)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DCHECK_DEFAULT ON)
else ()
    set(DCHECK_DEFAULT OFF)
endif ()

option(DCHECKS_ENABLED "Enable DCHECKs" ${DCHECK_DEFAULT})

file(GLOB bjvm_SRC CONFIGURE_DEPENDS "*.c" "*.h")
file(GLOB bjvm_wasm_SRC CONFIGURE_DEPENDS "wasm/*.c" "wasm/*.h")

# this definitely should be wrapped in an if (EMSCRIPTEN), and we should run the tests cases by
# somehow emulating emscripten locally...
list(APPEND bjvm_SRC ${bjvm_wasm_SRC})

add_library(vm OBJECT ${bjvm_SRC})
target_include_directories(vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vm PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

set_source_files_properties(
        interpreter2.c
        PROPERTIES
        COMPILE_FLAGS "-fno-sanitize=address"
)

target_link_libraries(vm PUBLIC stb_ds)
target_link_libraries(vm PRIVATE m)

if (NOT EMSCRIPTEN)
    FetchContent_MakeAvailable(zlib)
    target_link_libraries(vm PRIVATE z)
else ()
    # DO NOT USE -O3 FOR EMSCRIPTEN. IT BREAKS THE POST-PROCESSOR BECAUSE IT MANGLES NAMES. IT ALSO BLOATS
    # THE BINARY.
    target_compile_options(vm PRIVATE "-sUSE_ZLIB" "-O2" "-matomics")
    target_link_options(vm PUBLIC "-sUSE_ZLIB" "-Wl,--whole-archive")
endif ()

if (APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_dtrace_support(vm "${CMAKE_CURRENT_SOURCE_DIR}/probes.d")
    set(DTRACE_ENABLED TRUE)
    message("Added dtrace support")
endif ()

check_symbol_exists(getenv stdlib.h HAVE_GETENV)
check_symbol_exists(fopen stdio.h HAVE_FOPEN)
check_include_file(byteswap.h HAVE_BYTESWAP_H)
check_include_file(libkern/OSByteOrder.h HAVE_OSBYTEORDER_H)

configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h")

target_compile_options(vm PRIVATE -Wno-c99-designator -Wl,--whole_archive -Wno-unused-command-line-argument -Werror=sign-compare -Wall -Werror -Werror=uninitialized -Wno-pragmas -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-zero-length -Wno-atomic-alignment)