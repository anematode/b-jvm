function(add_dtrace_support target probes_definitions)
    get_filename_component(basename "${probes_definitions}" NAME_WE)
    set(probes_dir "${CMAKE_CURRENT_BINARY_DIR}/probes")
    set(probes_object "${probes_dir}/${basename}.o")
    set(probes_header "${probes_dir}/${basename}.h")

    file(MAKE_DIRECTORY ${probes_dir})

    target_include_directories(${target} PRIVATE ${probes_dir})

    add_custom_command(
            OUTPUT ${probes_header}
            COMMAND dtrace -h -s ${probes_definitions} -o ${probes_header}
            DEPENDS ${probes_definitions}
            COMMENT "Generating DTrace header ${probes_header}"
    )
    add_custom_target(generate_probe_header DEPENDS ${probes_header})
    add_dependencies(${target} generate_probe_header)
endfunction()