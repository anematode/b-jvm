function(add_dtrace_support target probes_definitions)
    get_filename_component(basename "${probes_definitions}" NAME)
    set(probes_dir "${CMAKE_CURRENT_BINARY_DIR}/probes")
    set(probes_object "${probes_dir}/${basename}.o")
    set(probes_header "${probes_dir}/${basename}.o")

    file(MAKE_DIRECTORY ${probes_dir})

    # Add compile definitions and include directories
    target_compile_definitions(${target} PRIVATE DTRACE_SUPPORT)
    target_include_directories(${target} PRIVATE ${probes_dir})

    # Generate DTrace header file
    add_custom_command(
            OUTPUT ${probes_header}
            COMMAND dtrace -h -s ${probes_definitions} -o ${probes_header}
            DEPENDS ${probes_definitions}
            COMMENT "Generating DTrace header ${probes_header}"
    )
    add_custom_target(generate_probe_header DEPENDS ${probes_header})
    add_dependencies(${target} generate_probe_header)

    if (NOT APPLE)
        # Generate DTrace DOF object
        add_custom_command(
                OUTPUT ${probes_object}
                COMMAND dtrace -G -s ${probes_definitions} -o ${probes_object}
                DEPENDS ${probes_definitions}
                COMMENT "Generating DTrace DOF object probes.o"
        )

        set_property(GLOBAL APPEND PROPERTY BJVM_LINK_OBJECTS ${probes_object})
    endif()
endfunction()