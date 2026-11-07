cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0057 NEW)

# cmake -DEXE=<exe> -DDEPS_BIN=<msys2 ucrt64/bin> -DOBJDUMP=<objdump> -P CopyDeps.cmake
#
# Walks the PE import tables starting from EXE and copies every DLL that
# lives in DEPS_BIN next to the executable. DLLs that come from Windows or
# from the compiler runtime are skipped.

if (NOT OBJDUMP OR NOT EXISTS "${OBJDUMP}")
    find_program(OBJDUMP NAMES objdump)
endif()
if (NOT OBJDUMP)
    message(FATAL_ERROR "objdump not found; cannot resolve runtime DLLs")
endif()

get_filename_component(OUT_DIR "${EXE}" DIRECTORY)

set(SKIP_DLLS
    libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll
)

set(queue "${EXE}")
set(seen "")

while (queue)
    list(POP_FRONT queue current)
    execute_process(COMMAND "${OBJDUMP}" -p "${current}"
                    OUTPUT_VARIABLE dump ERROR_QUIET)
    string(REGEX MATCHALL "DLL Name: [^\r\n]+" names "${dump}")
    foreach(entry IN LISTS names)
        string(REGEX REPLACE "DLL Name: " "" dll "${entry}")
        string(STRIP "${dll}" dll)
        string(TOLOWER "${dll}" dll_lc)
        list(FIND seen "${dll_lc}" _i_seen)
        list(FIND SKIP_DLLS "${dll_lc}" _i_skip)
        if (NOT _i_seen EQUAL -1 OR NOT _i_skip EQUAL -1)
            continue()
        endif()
        # Qt itself always comes from the Qt kit, never from MSYS2.
        if (dll_lc MATCHES "^qt[0-9]")
            continue()
        endif()
        list(APPEND seen "${dll_lc}")
        if (EXISTS "${DEPS_BIN}/${dll}")
            file(COPY "${DEPS_BIN}/${dll}" DESTINATION "${OUT_DIR}")
            list(APPEND queue "${DEPS_BIN}/${dll}")
            message(STATUS "deploy: ${dll}")
        endif()
    endforeach()
endwhile()
