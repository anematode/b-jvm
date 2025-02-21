set(CMAKE_CXX_STANDARD 26)

include(AddNodeShebang)
include(PostprocessWasm)
include(Doctest)

find_package(OpenMP)

add_executable(tests tests.cc tests-main.cc arrays-test.cc tests-common.cc wasm-tests.cc
        async-test.cc
        analysis-tests.cc
        classpath-tests.cc
        natives-test.cc
        benches.cc)
run_emscripten_postprocess(tests)

if (EMSCRIPTEN)
    AddNodeShebang(tests)
endif()

if (NOT EMSCRIPTEN)
    target_link_libraries(tests PRIVATE z)
    target_link_libraries(tests PRIVATE bjvm_static)
else()
    target_link_libraries(tests PRIVATE "-Wl,--whole-archive" bjvm_static)
    target_link_options(tests PRIVATE -sNODERAWFS)  # We're running our tests in Node, so just do this
endif()

target_link_libraries(tests PUBLIC doctest::doctest)

if(OpenMP_CXX_FOUND)
    target_link_libraries(tests PUBLIC OpenMP::OpenMP_CXX)
endif()

if (NOT EMSCRIPTEN)
add_test(NAME tests
        COMMAND tests --skip-benchmarks
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
else()
add_test(NAME tests
        COMMAND nvm use --lts && node ${CMAKE_SOURCE_DIR}/build/test/tests.js --skip-benchmarks
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set_tests_properties(tests PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1,detect_container_overflow=0")
set_tests_properties(tests PROPERTIES ENVIRONMENT "BOOT_CLASSPATH=${CMAKE_CURRENT_SOURCE_DIR}/jdk23.jar")