target_compile_definitions(bjvm_prebuild PRIVATE DTRACE_SUPPORT)
target_include_directories(bjvm_prebuild PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/probes)

add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/probes/probes.h"
        COMMAND dtrace -h -s "${CMAKE_CURRENT_SOURCE_DIR}/src/probes.d"
        -o "${CMAKE_CURRENT_BINARY_DIR}/probes/probes.h"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/probes.d"
        COMMENT "Generating DTrace header probes.h"
)
add_custom_target(generate_probe_header
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/probes/probes.h"
)

add_dependencies(bjvm_prebuild generate_probe_header)

if (NOT APPLE)
    add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/probes/probes.o"
            COMMAND dtrace -G -s "${CMAKE_CURRENT_SOURCE_DIR}/src/probes.d"
            -o "${CMAKE_CURRENT_BINARY_DIR}/probes/probes.o"
            DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/probes.d"
            COMMENT "Generating DTrace DOF object probes/probes.o"
    )

    list(APPEND BJVM_LINK_OBJECTS "${CMAKE_CURRENT_BINARY_DIR}/probes/probes.o")
endif ()