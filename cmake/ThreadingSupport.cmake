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

function(CHECK_WORKING_PTHREADS varname)
    CHECK_SOURCE_COMPILES(C "
        #include <pthread.h>
        #include <stdio.h>

        void *thread_task(void *ptr) {
            printf(\"input %s\n\", (char *) ptr);
            return ptr;
        }

        int main(int argc, char **argv) {
            pthread_t thread1, thread2;
            char *input1 = \"a\", *input2 = \"b\";
            // start the threads
            pthread_create(&thread1, NULL, *thread_task, (void *) input1);
            pthread_create(&thread2, NULL, *thread_task, (void *) input2);
            // wait for threads to finish
            pthread_join(thread1, NULL);
            pthread_join(thread2, NULL);
            return 0;
        }
        " ${varname})
endfunction(CHECK_WORKING_PTHREADS)

message("Pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")
CHECK_WORKING_ATOMICS(HAVE_ATOMICS_WITHOUT_LIB)

if (HAVE_ATOMICS_WITHOUT_LIB)
    message("atomics already supported by gcc")
else ()
    message("adding link library atomic")
    target_link_libraries(bjvm_static PRIVATE atomic)
endif ()

CHECK_WORKING_ATOMICS(HAVE_PTHREADS_WITHOUT_LIB)

if (HAVE_PTHREADS_WITHOUT_LIB)
    message("pthreads already supported by gcc")
else ()
    message("adding link library pthread")
    target_link_libraries(bjvm_static PRIVATE pthread)
endif ()