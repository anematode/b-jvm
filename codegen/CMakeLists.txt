add_executable(gen_natives gen_natives.c)
target_link_libraries(gen_natives PRIVATE bjvm_static)

include(CheckCXXSourceCompiles)
include(CheckLibraryExists)

function(CHECK_WORKING_CXX_ATOMICS64 varname)
    CHECK_CXX_SOURCE_COMPILES("
        #include <atomic>
        #include <cstdint>
        std::atomic<uint64_t> x (0);
        int main() {
          uint64_t i = x.load(std::memory_order_relaxed);
          return 0;
        }
        " ${varname})
endfunction(CHECK_WORKING_CXX_ATOMICS64)

message("Pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")

CHECK_WORKING_CXX_ATOMICS64(HAVE_CXX_ATOMICS64_WITHOUT_LIB)
if (NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB)
    message("adding link library atomic")
    target_link_libraries(vm PRIVATE atomic)
endif ()