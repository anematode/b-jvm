include(CheckIPOSupported)

check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT error)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LTO_DEFAULT OFF)
else ()
    set(LTO_DEFAULT LTO_SUPPORTED)
endif ()

option(ENABLE_LTO "Enable link-time optimisation" ${LTO_DEFAULT})

if (ENABLE_LTO)
    if (LTO_SUPPORTED)
        message(STATUS "IPO / LTO enabled")
        set_property(TARGET bjvm PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO / LTO not supported: <${error}>")
    endif()
endif()