add_library(stb_ds STATIC stb_ds.c stb_ds.h)
target_include_directories(stb_ds PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})