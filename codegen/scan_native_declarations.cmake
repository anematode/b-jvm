function (generate_push_call pkg_path class_name)

endfunction()

function (process_file filename)

    file(READ "${filename}" contents)
    string(REGEX MATCHALL ${export_regex} MATCHES ${contents})

    foreach(MATCH ${MATCHES})
        string(REGEX REPLACE ${export_regex} "" FIELDS ${MATCH})

        list(GET FIELDS 0 package_path)
        list(GET FIELDS 1 class_name)
        list(GET FIELDS 2 method_name)
        list(GET FIELDS 3 method_descriptor)
        list(GET FIELDS 4 async_ctx_size)
        list(GET FIELDS 5 modifier)

        # push_bjvm_native(                                                                                                  \
        #        STR(package_path "/" #class_name_), STR(#method_name_), STR(method_descriptor_),                               \
        #        (bjvm_native_callback){.async_ctx_bytes = async_sz,                                                            \
        #                               .variant = (variant##_native_callback) & class_name_##_##method_name_##_cb##modifier}); \
        set(native_method_name "${class_name}_${method_name}_cb${modifier}")
        if (async_ctx_size STREQUAL "0")
            set(callback_union_variant "sync")
        else()
            set(callback_union_variant "async")
        endif ()

        set(push_call "
                push_bjvm_native(
                    STR(${package_path} \"/\" ${class_name}),
                    STR(${method_name}),
                    STR(${method_descriptor}),
                    (bjvm_native_callback) {
                        .async_ctx_bytes = async_sz,
                        .${callback_union_variant} = (void *) &${native_method_name});
                );
        ")

        message(${push_call})
    endforeach()
endfunction()

function (add_classes_root directory_name)
    file(GLOB_RECURSE classes_sources CONFIGURE_DEPENDS "${directory_name}/*.c")
    target_sources(natives PRIVATE ${classes_sources})
    get_filename_component(abs_dir ${directory_name} ABSOLUTE)

    foreach(file IN LISTS classes_sources)
        get_filename_component(abs_src ${file} ABSOLUTE)
        file(RELATIVE_PATH rel_path "${directory_name}" "${abs_src}")

        get_filename_component(dir_part "${rel_path}" DIRECTORY)
        get_filename_component(file_part "${rel_path}" NAME)

        set(out_dir "${CMAKE_BINARY_DIR}/preprocessed/${dir_part}")
        file(MAKE_DIRECTORY "${out_dir}")
        get_filename_component(file_name_no_ext "${file_part}" NAME_WE)
        set(out_file "${out_dir}/${file_name_no_ext}.i")

        add_custom_command(
                OUTPUT "${out_file}"
                COMMAND "${CMAKE_C_COMPILER}"
                -E
                $<JOIN:$<TARGET_PROPERTY:${natives},INTERFACE_COMPILE_OPTIONS>, >
                "${abs_src}"
                -o "${out_file}"
                DEPENDS "${abs_src}"
                COMMENT "Preprocessing ${src} => ${out_file}"
                VERBATIM
        )

        add_custom_target("preprocess_${file_name_no_ext}"
                DEPENDS "${out_file}"
        )
        add_dependencies(preprocess_all_files "preprocess_${file_name_no_ext}")
    endforeach ()
endfunction()

process_file("../natives/share/java/io/FileDescriptor.c")