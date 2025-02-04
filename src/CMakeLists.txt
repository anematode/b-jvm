include(CheckSymbolExists)

file(GLOB bjvm_SRC CONFIGURE_DEPENDS "*.c" "*.h")
file(GLOB bjvm_wasm_SRC CONFIGURE_DEPENDS "wasm/*.c" "wasm/*.h")

# this definitely should be wrapped in an if (EMSCRIPTEN), and we should run the tests cases by
# somehow emulating emscripten locally...
list(APPEND bjvm_SRC ${bjvm_wasm_SRC})

add_library(bjvm_prebuild OBJECT ${bjvm_SRC})
target_include_directories(bjvm_prebuild PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(bjvm_prebuild PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(bjvm_prebuild PUBLIC stb_ds)
target_link_libraries(bjvm_prebuild PRIVATE m)

if (NOT EMSCRIPTEN) # emscripten comes with its own
    target_link_libraries(bjvm_prebuild PRIVATE z)
endif ()

if (APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_dtrace_support(bjvm_prebuild "${CMAKE_CURRENT_SOURCE_DIR}/probes.d")
    set(DTRACE_ENABLED TRUE)
    message("Added dtrace support")
endif()

check_symbol_exists(getenv stdlib.h HAVE_GETENV)
check_symbol_exists(fopen stdio.h HAVE_FOPEN)

configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h")
