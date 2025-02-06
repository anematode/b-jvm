add_library(natives OBJECT)
target_link_libraries(natives PRIVATE vm)

# all configurations need the DSL stuff to be built
file(GLOB DSL_SOURCES CONFIGURE_DEPENDS "dsl/*.c")
file(GLOB DSL_HEADERS CONFIGURE_DEPENDS "dsl/*.h")

target_sources(natives PRIVATE ${DSL_SOURCES} ${DSL_HEADERS})
target_include_directories(natives PUBLIC "dsl")

# adds a directory that contains natives organized like a typical Java package hierarchy, but with c files instead of
# .java files.
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

add_classes_root("${CMAKE_CURRENT_SOURCE_DIR}/share")

if (EMSCRIPTEN)
    add_classes_root("${CMAKE_CURRENT_SOURCE_DIR}/emscripten")
endif ()

if (UNIX)
    add_classes_root("${CMAKE_CURRENT_SOURCE_DIR}/unix")
endif()

if (APPLE)
    add_classes_root("${CMAKE_CURRENT_SOURCE_DIR}/darwin")
endif()