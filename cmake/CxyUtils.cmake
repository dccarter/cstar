macro(CxyAmalgamate name)
    set(options)
    set(kvargs OUTDIR VARNAME)
    set(kvvargs FILES)
    cmake_parse_arguments(CXY_AMALGAMATE "${options}" "${kvargs}" "${kvvargs}" ${ARGN})

    string(REPLACE "-" "_" CXY_AMALGAMATE_FILENAME ${name})

    if (NOT CXY_AMALGAMATE_FILES)
        message(FATAL_ERROR "expecting at least 1 file name")
    endif ()

    if (NOT CXY_AMALGAMATE_OUTDIR)
        set(CXY_AMALGAMATE_OUTDIR ${CMAKE_CURRENT_BINARY_DIR})
    endif ()

    if (NOT CXY_AMALGAMATE_VARNAME)
        string(TOUPPER ${CXY_AMALGAMATE_FILENAME} CXY_AMALGAMATE_VARNAME)
    endif ()

    file(MAKE_DIRECTORY ${CXY_AMALGAMATE_OUTDIR}/src)

    add_custom_command(
            OUTPUT ${CXY_AMALGAMATE_OUTDIR}/src/${CXY_AMALGAMATE_FILENAME}.c
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMAND amalgamate ${CXY_AMALGAMATE_OUTDIR}/src
            ${CXY_AMALGAMATE_FILENAME}
            ${CXY_AMALGAMATE_VARNAME}
            ${CXY_AMALGAMATE_FILES}
            DEPENDS ${CXY_AMALGAMATE_FILES} amalgamate)

    include_directories(${CXY_AMALGAMATE_OUTDIR})
endmacro()
