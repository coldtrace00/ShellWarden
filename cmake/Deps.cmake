# Locates libssh, libsodium and libvterm and exposes them as `shellwarden_deps`.

add_library(shellwarden_deps INTERFACE)

if (WIN32)
    set(SHELLWARDEN_DEPS_PREFIX "C:/msys64/ucrt64" CACHE PATH
        "MSYS2 UCRT64 prefix that contains libssh, libsodium and libvterm")

    if (NOT EXISTS "${SHELLWARDEN_DEPS_PREFIX}/include/libssh/libssh.h")
        message(FATAL_ERROR
            "libssh not found in ${SHELLWARDEN_DEPS_PREFIX}.\n"
            "Open 'MSYS2 UCRT64' and run:\n"
            "  pacman -S --needed mingw-w64-ucrt-x86_64-libssh mingw-w64-ucrt-x86_64-libsodium mingw-w64-ucrt-x86_64-libvterm\n"
            "or set SHELLWARDEN_DEPS_PREFIX to your MSYS2 ucrt64 folder.")
    endif()

    # Libraries are referenced by full path (no -L) so nothing else from
    # MSYS2 (libstdc++, libgcc) can leak into the link.
    find_library(SW_SSH_LIB    NAMES ssh.dll    ssh    PATHS "${SHELLWARDEN_DEPS_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(SW_SODIUM_LIB NAMES sodium.dll sodium PATHS "${SHELLWARDEN_DEPS_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(SW_VTERM_LIB  NAMES vterm.dll  vterm  PATHS "${SHELLWARDEN_DEPS_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    # OpenSSL comes with libssh in MSYS2 (used for PuTTY .ppk import).
    find_library(SW_CRYPTO_LIB NAMES crypto.dll crypto PATHS "${SHELLWARDEN_DEPS_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)

    # -idirafter: the toolchain's own C runtime headers always win; the MSYS2
    # include folder is only consulted for headers the toolchain doesn't have
    # (libssh/, sodium/, vterm.h).
    target_compile_options(shellwarden_deps INTERFACE "-idirafter${SHELLWARDEN_DEPS_PREFIX}/include")
    target_link_libraries(shellwarden_deps INTERFACE ${SW_SSH_LIB} ${SW_SODIUM_LIB} ${SW_VTERM_LIB} ${SW_CRYPTO_LIB})

    # After every build, copies exactly the MSYS2 DLLs the executable needs
    # (libssh, libcrypto, zlib, libsodium, libvterm and whatever they pull in),
    # resolved by walking the import tables. The compiler runtime (libgcc,
    # libstdc++, winpthread) is NOT copied: Qt's MinGW kit provides it.
    function(shellwarden_copy_runtime_dlls target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DEXE=$<TARGET_FILE:${target}>"
                "-DDEPS_BIN=${SHELLWARDEN_DEPS_PREFIX}/bin"
                "-DOBJDUMP=${CMAKE_OBJDUMP}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CopyDeps.cmake"
            VERBATIM)
    endfunction()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SW_PKG REQUIRED IMPORTED_TARGET libssh libsodium vterm libcrypto)
    target_link_libraries(shellwarden_deps INTERFACE PkgConfig::SW_PKG)
endif()
