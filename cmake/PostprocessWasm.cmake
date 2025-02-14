function(run_emscripten_postprocess TARGET)
    if (EMSCRIPTEN)
        #target_compile_options(${TARGET} PUBLIC "-fno-inline")
        target_link_options(${TARGET} PUBLIC "--profiling-funcs" "-O0")  # keep names

        set(POST_PROCESS_SCRIPT ${CMAKE_SOURCE_DIR}/codegen/wasm-opt.js)
        # remove .js extension, add .wasm extension
        set(THE_FILE $<TARGET_FILE_DIR:${TARGET}>/$<TARGET_FILE_BASE_NAME:${TARGET}>.wasm)

        add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND node ${POST_PROCESS_SCRIPT} ${THE_FILE} -O0 --debuginfo -all --interpreter -o ${THE_FILE}
                COMMENT "Postprocessing wasm file => ${THE_FILE}"
                VERBATIM
        )
    endif()
endfunction()