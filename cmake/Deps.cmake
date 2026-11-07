# Locates libssh, libsodium and libvterm and exposes them as `shellwarden_deps`.
#
# Minimum versions (security):
#   libssh    0.10.6  — fixes Terrapin (CVE-2023-48795) and CVE-2023-6004/6918
#   libsodium 1.0.18
#   OpenSSL   3.0     — 1.1.1 is out of support
# A distribution that backported the fixes into an older libssh can build with
# -DSHELLWARDEN_ALLOW_OLD_LIBSSH=ON (at its own responsibility).

add_library(shellwarden_deps INTERFACE)

set(SW_MIN_LIBSSH "0.10.6")
set(SW_MIN_SODIUM "1.0.18")
set(SW_MIN_OPENSSL "3.0.0")
option(SHELLWARDEN_ALLOW_OLD_LIBSSH "Build with libssh older than ${SW_MIN_LIBSSH} (only if your distribution backported the security fixes)" OFF)
if (SHELLWARDEN_ALLOW_OLD_LIBSSH)
    target_compile_definitions(shellwarden_deps INTERFACE SHELLWARDEN_ALLOW_OLD_LIBSSH)
endif()

function(sw_require_version what found minimum)
    if (found VERSION_LESS minimum)
        if (what STREQUAL "libssh" AND SHELLWARDEN_ALLOW_OLD_LIBSSH)
            message(WARNING "libssh ${found} is older than ${minimum}: make sure the Terrapin fix (CVE-2023-48795) is backported.")
        else()
            message(FATAL_ERROR "${what} ${found} is too old: ${minimum} or newer is required (security fixes).")
        endif()
    else()
        message(STATUS "${what} ${found} (minimum ${minimum})")
    endif()
endfunction()

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

    # versions, read from the headers
    set(_inc "${SHELLWARDEN_DEPS_PREFIX}/include")
    set(_sshver "${_inc}/libssh/libssh_version.h")
    if (NOT EXISTS "${_sshver}")
        set(_sshver "${_inc}/libssh/libssh.h")
    endif()
    file(STRINGS "${_sshver}" _l REGEX "#define LIBSSH_VERSION_(MAJOR|MINOR|MICRO) +[0-9]+")
    string(REGEX REPLACE ".*MAJOR +([0-9]+).*" "\\1" _ma "${_l}")
    string(REGEX REPLACE ".*MINOR +([0-9]+).*" "\\1" _mi "${_l}")
    string(REGEX REPLACE ".*MICRO +([0-9]+).*" "\\1" _mc "${_l}")
    sw_require_version(libssh "${_ma}.${_mi}.${_mc}" ${SW_MIN_LIBSSH})
    file(STRINGS "${_inc}/sodium/version.h" _l REGEX "#define SODIUM_VERSION_STRING")
    string(REGEX REPLACE ".*\"([0-9.]+)\".*" "\\1" _sv "${_l}")
    sw_require_version(libsodium "${_sv}" ${SW_MIN_SODIUM})
    file(STRINGS "${_inc}/openssl/opensslv.h" _l REGEX "#[ ]*define OPENSSL_VERSION_(MAJOR|MINOR|PATCH) +[0-9]+")
    string(REGEX REPLACE ".*MAJOR +([0-9]+).*" "\\1" _ma "${_l}")
    string(REGEX REPLACE ".*MINOR +([0-9]+).*" "\\1" _mi "${_l}")
    string(REGEX REPLACE ".*PATCH +([0-9]+).*" "\\1" _mc "${_l}")
    sw_require_version(OpenSSL "${_ma}.${_mi}.${_mc}" ${SW_MIN_OPENSSL})

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
    sw_require_version(libssh "${SW_PKG_libssh_VERSION}" ${SW_MIN_LIBSSH})
    sw_require_version(libsodium "${SW_PKG_libsodium_VERSION}" ${SW_MIN_SODIUM})
    sw_require_version(OpenSSL "${SW_PKG_libcrypto_VERSION}" ${SW_MIN_OPENSSL})
endif()

# BLAKE3 (Shadowsocks 2022): vendored portable C, see third_party/blake3/README.md
add_library(sw_blake3 STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../third_party/blake3/blake3.c
    ${CMAKE_CURRENT_LIST_DIR}/../third_party/blake3/blake3_dispatch.c
    ${CMAKE_CURRENT_LIST_DIR}/../third_party/blake3/blake3_portable.c)
target_compile_definitions(sw_blake3 PRIVATE BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2 BLAKE3_NO_AVX512 BLAKE3_USE_NEON=0)
target_include_directories(sw_blake3 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../third_party/blake3)
set_target_properties(sw_blake3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(shellwarden_deps INTERFACE sw_blake3)
