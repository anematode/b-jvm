add_library(natives OBJECT)
target_link_libraries(natives PUBLIC bjvm_prebuild)

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