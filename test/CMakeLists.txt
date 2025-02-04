set(CMAKE_CXX_STANDARD 26)

find_package(OpenMP)

add_executable(tests tests.cc arrays-test.cc tests-common.cc wasm-tests.cc
        async-test.cc
        analysis-tests.cc
        classpath-tests.cc
        natives-test.cc
        benches.cc)

target_link_libraries(tests PRIVATE z)
target_link_libraries(tests PRIVATE bjvm natives )
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
set_target_properties(tests PROPERTIES LINK_FLAGS "${EmscriptenFlags}")

if(OpenMP_CXX_FOUND)
    target_link_libraries(tests PUBLIC OpenMP::OpenMP_CXX)
endif()

add_test(NAME tests
        COMMAND tests --skip-benchmarks
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(tests PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1,detect_container_overflow=0")
set_tests_properties(tests PROPERTIES ENVIRONMENT "BOOT_CLASSPATH=${CMAKE_CURRENT_SOURCE_DIR}/jdk23.jar")