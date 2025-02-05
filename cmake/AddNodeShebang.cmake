function(AddNodeShebang target_name)
    add_custom_command(
            TARGET ${target_name} POST_BUILD
            COMMAND echo "\\#!/usr/bin/env node" > "$<TARGET_FILE:${target_name}>.tmp"
            COMMAND cat "$<TARGET_FILE:${target_name}>" >> "$<TARGET_FILE:${target_name}>.tmp"
            COMMAND mv "$<TARGET_FILE:${target_name}>.tmp" "$<TARGET_FILE:${target_name}>"
            COMMAND chmod +x "$<TARGET_FILE:${target_name}>"
            COMMENT "Adding Node.js shebang to $<TARGET_FILE:${target_name}>"
    )
endfunction()
