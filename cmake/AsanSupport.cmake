if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ASAN_DEFAULT ON)
else ()
    set(ASAN_DEFAULT OFF)
endif ()

option(ASAN "Enable ASan and UBSan" ${ASAN_DEFAULT})

if (ASAN)
    set(ASAN_FLAGS
            -fno-omit-frame-pointer
            -fsanitize=address
            -fsanitize-recover=address
            -fsanitize-address-use-after-scope)

    add_compile_options(${ASAN_FLAGS})
    add_link_options(${ASAN_FLAGS})
endif ()