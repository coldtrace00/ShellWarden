# Tests (cmake -DSHELLWARDEN_BUILD_TESTS=ON). See tests/README.md.
#   unit_tests         no network, runs anywhere
#   integration_tests  needs the servers from tests/integration/setup.sh (Linux)
#   fuzz_*             libFuzzer targets (clang, -DSHELLWARDEN_FUZZ=ON)

set(SW_SRC ${PROJECT_SOURCE_DIR}/src)
find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)

# the core sources the tests use, as one library
add_library(sw_core_for_tests STATIC
    ${SW_SRC}/core/Vault.cpp ${SW_SRC}/core/Minisign.cpp ${SW_SRC}/core/Shadowsocks.cpp
    ${SW_SRC}/core/KeyTools.cpp ${SW_SRC}/core/KnownHosts.cpp ${SW_SRC}/core/SshWorker.cpp ${SW_SRC}/core/SshWorker.h
    ${SW_SRC}/core/SshSftp.cpp ${SW_SRC}/core/SshForward.cpp ${SW_SRC}/core/JumpBridge.cpp ${SW_SRC}/core/ProxyTunnel.cpp
    ${SW_SRC}/core/Paths.cpp ${SW_SRC}/core/PrivateDir.cpp ${SW_SRC}/core/PrivateDirWin.cpp ${SW_SRC}/core/FileNames.cpp
    ${SW_SRC}/core/DepVersions.cpp ${SW_SRC}/core/Protected.cpp ${SW_SRC}/core/CommandHistory.cpp
    ${SW_SRC}/core/PasswordTools.cpp ${SW_SRC}/core/ActivityLog.cpp ${SW_SRC}/core/ActivityLog.h ${SW_SRC}/core/FirewallLock.cpp
    ${SW_SRC}/core/AgentClient.cpp ${SW_SRC}/core/HostImport.cpp
    ${SW_SRC}/app/UpdateChecker.cpp ${SW_SRC}/app/UpdateChecker.h)
set_target_properties(sw_core_for_tests PROPERTIES AUTOMOC ON)
target_include_directories(sw_core_for_tests PUBLIC ${SW_SRC})
target_compile_definitions(sw_core_for_tests PUBLIC
    PROJECT_VERSION_STRING="${PROJECT_VERSION}" APP_NAME_STRING="ShellWarden"
    APP_GITHUB_REPO="" APP_UPDATE_PUBKEY="")
target_link_libraries(sw_core_for_tests PUBLIC Qt6::Core Qt6::Network Qt6::Gui shellwarden_deps)
if (WIN32)
    target_compile_definitions(sw_core_for_tests PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(sw_core_for_tests PUBLIC ws2_32 advapi32)
endif()

add_executable(unit_tests unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE sw_core_for_tests)
add_test(NAME unit_tests COMMAND unit_tests)

if (UNIX)
    add_executable(integration_tests integration_tests.cpp)
    target_link_libraries(integration_tests PRIVATE sw_core_for_tests)
    # not part of plain `ctest`: needs tests/integration/setup.sh first
    add_test(NAME integration_tests COMMAND integration_tests)
    set_tests_properties(integration_tests PROPERTIES LABELS integration)
endif()

option(SHELLWARDEN_FUZZ "Build libFuzzer targets (clang)" OFF)
if (SHELLWARDEN_FUZZ)
    foreach(t ppk_import ss_link ssh_config vault_file minisign known_hosts journal_line ss_stream)
        add_executable(fuzz_${t} fuzz/fuzz_${t}.cpp)
        target_link_libraries(fuzz_${t} PRIVATE sw_core_for_tests)
        target_compile_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
        target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
    endforeach()
endif()
