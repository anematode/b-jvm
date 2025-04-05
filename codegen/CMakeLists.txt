add_executable(gen_natives gen_natives.c)
target_link_libraries(gen_natives PRIVATE bjvm_static)

include(CheckSourceCompiles)
include(CheckLibraryExists)

function(CHECK_WORKING_ATOMICS varname)
    CHECK_SOURCE_COMPILES(C "
        #include <stdatomic.h>
        #include <stdint.h>

        #define SIZEOF_POINTER (UINTPTR_MAX / 255 % 255)
        #if SIZEOF_POINTER == 4
        typedef uint64_t extended_data_t;
        #else
        typedef __uint128_t extended_data_t;
        #endif

        extended_data_t x; // sized to fit two void pointers

        int main() {
            extended_data_t i = __atomic_load_n(&x, __ATOMIC_SEQ_CST);
            return 0;
        }
        " ${varname})
endfunction(CHECK_WORKING_ATOMICS)

message("Pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")
CHECK_WORKING_ATOMICS(HAVE_ATOMICS_WITHOUT_LIB)
if (NOT HAVE_ATOMICS_WITHOUT_LIB)
    message("adding link library atomic")
    target_link_libraries(vm PRIVATE atomic)
endif ()
