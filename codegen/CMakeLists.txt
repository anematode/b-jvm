add_executable(gen_natives gen_natives.c)
target_link_libraries(gen_natives PRIVATE bjvm_static)

if (NOT EMSCRIPTEN)
    target_link_options(vm PRIVATE -mcx16 -latomic)
else ()
    # DO NOT USE -O3 FOR EMSCRIPTEN. IT BREAKS THE POST-PROCESSOR BECAUSE IT MANGLES NAMES. IT ALSO BLOATS
    # THE BINARY.
    target_compile_options(vm PRIVATE "-sUSE_ZLIB" "-O2" "-matomics")
endif ()