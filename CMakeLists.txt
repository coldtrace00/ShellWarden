cmake_minimum_required(VERSION 3.21)

project(ShellWarden VERSION 0.4.1 LANGUAGES C CXX)

# ---- branding: change the product name in ONE place --------------------
set(APP_NAME        "ShellWarden"  CACHE STRING "Product name (window, data folder, installer)")
set(APP_GITHUB_REPO "coldtrace00/ShellWarden" CACHE STRING "owner/repo used by the in-app updater (empty = no update check)")
# minisign public key that signs releases (SHA256SUMS.minisig). Updates are installed only
# when they are signed with it. Empty = the official key from minisign.pub in the project
# root; a fork puts its own key there (or passes -DAPP_UPDATE_PUBKEY=RW…); "none" = no check.
set(APP_UPDATE_PUBKEY ""         CACHE STRING "minisign public key (RW…); empty = minisign.pub, none = no signature check")
if (APP_UPDATE_PUBKEY STREQUAL "none")
    set(APP_UPDATE_PUBKEY_EFFECTIVE "")
elseif (APP_UPDATE_PUBKEY STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/minisign.pub")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/minisign.pub" _pub REGEX "^RW[A-Za-z0-9+/=]+$" LIMIT_COUNT 1)
    set(APP_UPDATE_PUBKEY_EFFECTIVE "${_pub}")
else()
    set(APP_UPDATE_PUBKEY_EFFECTIVE "${APP_UPDATE_PUBKEY}")
endif()
if (APP_UPDATE_PUBKEY_EFFECTIVE)
    message(STATUS "Updates must be signed with minisign key ${APP_UPDATE_PUBKEY_EFFECTIVE}")
else()
    message(STATUS "Update signature check is OFF (SHA-256 only)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network LinguistTools)
find_package(Qt6 QUIET COMPONENTS SerialPort)
if (NOT WIN32)
    find_package(Qt6 QUIET COMPONENTS DBus)   # lock on screen lock / sleep (logind)
endif()
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Third-party C libraries: libssh, libsodium, libvterm
#
# Windows: taken from MSYS2 UCRT64 (same C runtime as Qt's MinGW 13.1 kit).
#   pacman -S --needed mingw-w64-ucrt-x86_64-libssh \
#                      mingw-w64-ucrt-x86_64-libsodium \
#                      mingw-w64-ucrt-x86_64-libvterm
# Linux: system packages (libssh-dev libsodium-dev libvterm-dev).
# ---------------------------------------------------------------------------
include(cmake/Deps.cmake)

qt_add_executable(ShellWardenApp WIN32
    src/main.cpp

    src/core/SecureBytes.h
    src/core/Vault.h            src/core/Vault.cpp
    src/core/Minisign.h         src/core/Minisign.cpp
    src/core/Shadowsocks.h      src/core/Shadowsocks.cpp
    src/core/BackgroundThreads.h src/core/BackgroundThreads.cpp
    src/core/SafeText.h
    src/core/KeyTools.h         src/core/KeyTools.cpp
    src/core/KnownHosts.h       src/core/KnownHosts.cpp
    src/core/SshWorker.h        src/core/SshWorker.cpp
    src/core/SshSftp.cpp        src/core/SshForward.cpp
    src/core/JumpBridge.h       src/core/JumpBridge.cpp
    src/core/ProxyTunnel.h      src/core/ProxyTunnel.cpp
    src/core/Paths.h            src/core/Paths.cpp
    src/core/PrivateDir.h       src/core/PrivateDir.cpp       src/core/PrivateDirWin.cpp
    src/core/FileNames.h        src/core/FileNames.cpp
    src/core/DepVersions.h      src/core/DepVersions.cpp
    src/core/Protected.h        src/core/Protected.cpp
    src/core/CommandHistory.h   src/core/CommandHistory.cpp
    src/core/PasswordTools.h    src/core/PasswordTools.cpp
    src/core/ActivityLog.h      src/core/ActivityLog.cpp
    src/core/FirewallLock.h     src/core/FirewallLock.cpp
    src/core/AgentClient.h src/core/AgentClient.cpp
    src/core/HostImport.h       src/core/HostImport.cpp

    src/terminal/TerminalItem.h src/terminal/TerminalItem.cpp
    src/terminal/PasteGuard.h
    src/terminal/TerminalEmulator.h src/terminal/TerminalEmulator.cpp

    src/app/VaultListModel.h    src/app/VaultListModel.cpp
    src/app/SessionListModel.h  src/app/SessionListModel.cpp
    src/app/ProxyChecker.h      src/app/ProxyChecker.cpp
    src/app/UpdateChecker.h     src/app/UpdateChecker.cpp
    src/app/QmlSafety.h         src/app/QmlSafety.cpp
    src/app/SystemLockWatcher.h src/app/SystemLockWatcher.cpp
    src/app/TorManager.h        src/app/TorManager.cpp
    src/app/DarkTitleBar.h      src/app/DarkTitleBar.cpp
    src/app/SyntaxHighlighter.h src/app/SyntaxHighlighter.cpp
    src/app/CloudflaredManager.h src/app/CloudflaredManager.cpp
    src/app/BatchRunner.h       src/app/BatchRunner.cpp
    src/app/LocalPty.h          src/app/LocalPty.cpp
    src/app/TerminalSession.h   src/app/TerminalSession.cpp
    src/app/AppController.h     src/app/AppController.cpp
    src/app/AppControllerProtect.cpp
    src/app/AppControllerPrivate.h
    src/app/AppControllerHosts.cpp
    src/app/AppControllerKeys.cpp
    src/app/AppControllerProxies.cpp
    src/app/AppControllerSessions.cpp
    src/app/AppControllerBatch.cpp
)

set_source_files_properties(qml/Theme.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(ShellWardenApp
    URI ShellWarden
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icons.qml
        qml/components/AppButton.qml
        qml/components/AppField.qml
        qml/components/IconLabel.qml
        qml/components/SideNavButton.qml
        qml/components/SessionTabButton.qml
        qml/components/Modal.qml
        qml/components/MasterPrompt.qml
        qml/components/PlainToolTips.qml
        qml/components/AppSwitch.qml
        qml/components/PasswordField.qml
        qml/components/Logo.qml
        qml/components/ThemePreview.qml
        qml/components/AppCombo.qml
        qml/components/AppMenu.qml
        qml/components/ThemeEditor.qml
        qml/components/QuickSearch.qml
        qml/components/CountryBadge.qml
        qml/components/CheckErrors.qml
        qml/components/ColorPicker.qml
        qml/components/AppMenuItem.qml
        qml/components/MonitorBar.qml
        qml/components/AppSpin.qml
        qml/components/AppCheckBox.qml
        qml/components/BatchSecrets.qml
        qml/pages/VaultGate.qml
        qml/pages/HostsPage.qml
        qml/pages/HostEditor.qml
        qml/pages/KeychainPage.qml
        qml/pages/KeyGenerateDialog.qml
        qml/pages/KeyImportDialog.qml
        qml/pages/KnownHostsPage.qml
        qml/pages/ProxiesPage.qml
        qml/pages/ProxyEditor.qml
        qml/pages/SettingsPage.qml
        qml/pages/TerminalPage.qml
        qml/pages/SftpPanel.qml
        qml/pages/TunnelsPanel.qml
        qml/pages/SnippetsPage.qml
        qml/pages/LogPage.qml
        qml/pages/ImportHostsDialog.qml
        qml/pages/BatchRunDialog.qml
        qml/pages/KeyDeployDialog.qml
        qml/pages/FileEditor.qml
        qml/pages/DiffDialog.qml
)

# QML type registration includes headers by file name only.
target_include_directories(ShellWardenApp PRIVATE src src/app src/terminal)
target_compile_definitions(ShellWardenApp PRIVATE
    PROJECT_VERSION_STRING="${PROJECT_VERSION}"
    APP_NAME_STRING="${APP_NAME}"
    APP_GITHUB_REPO="${APP_GITHUB_REPO}"
    APP_UPDATE_PUBKEY="${APP_UPDATE_PUBKEY_EFFECTIVE}")
if (TARGET Qt6::SerialPort)
    target_link_libraries(ShellWardenApp PRIVATE Qt6::SerialPort)
    target_compile_definitions(ShellWardenApp PRIVATE SW_HAVE_SERIAL)
endif()
if (UNIX AND NOT APPLE)
    target_link_libraries(ShellWardenApp PRIVATE util)   # forkpty() for the local terminal
endif()
if (TARGET Qt6::DBus)
    target_link_libraries(ShellWardenApp PRIVATE Qt6::DBus)
    target_compile_definitions(ShellWardenApp PRIVATE SW_HAVE_DBUS)
endif()

target_link_libraries(ShellWardenApp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network
    shellwarden_deps
)

if (WIN32)
    target_compile_definitions(ShellWardenApp PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(ShellWardenApp PRIVATE ws2_32 advapi32 wtsapi32 user32 dwmapi)
    shellwarden_copy_runtime_dlls(ShellWardenApp)
endif()

if (MSVC)
    target_compile_options(ShellWardenApp PRIVATE /W4)
else()
    target_compile_options(ShellWardenApp PRIVATE -Wall -Wextra)
endif()

# The QML module folder is also called "ShellWarden", so on Linux/macOS the
# binary keeps the target name to avoid a file/folder clash.
if (WIN32)
    enable_language(RC)
    set_target_properties(ShellWardenApp PROPERTIES OUTPUT_NAME "ShellWarden")
    # Exe icon (Explorer, taskbar). The .rc is generated so the .ico path is absolute.
    configure_file(branding/shellwarden.rc.in ${CMAKE_CURRENT_BINARY_DIR}/shellwarden.rc @ONLY)
    target_sources(ShellWardenApp PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shellwarden.rc)
else()
    set_target_properties(ShellWardenApp PROPERTIES OUTPUT_NAME "shellwarden")
endif()

# Country flags (flag-icons, MIT) and country names (i18n-iso-countries, MIT)
# All flags are in one picture (16 per row, 40x30 each); countries.json has the index.
qt_add_resources(ShellWardenApp "assets"
    PREFIX "/"
    FILES assets/flags.png assets/countries.json
)

# Window icon, used by main.cpp as :/branding/icon.png
qt_add_resources(ShellWardenApp "branding"
    PREFIX "/branding"
    BASE branding
    FILES branding/icon.png
)

# UI languages: English (source), Ukrainian, Russian, French, Spanish.
# Edit the .ts files with Qt Linguist; `cmake --build . --target update_translations`
# refreshes them from the sources. Compiled .qm files are embedded under :/i18n.
qt_add_translations(ShellWardenApp
    TS_FILES
        i18n/shellwarden_uk.ts
        i18n/shellwarden_ru.ts
        i18n/shellwarden_fr.ts
        i18n/shellwarden_es.ts
    RESOURCE_PREFIX "/i18n"
)

if (WIN32)
    install(TARGETS ShellWardenApp RUNTIME DESTINATION .)
else()
    include(GNUInstallDirs)
    install(TARGETS ShellWardenApp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/shellwarden.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES branding/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME shellwarden.png)
    install(FILES branding/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME shellwarden.svg)

    # cpack -G DEB  → shellwarden_<version>_amd64.deb
    set(CPACK_PACKAGE_NAME "shellwarden")
    set(CPACK_PACKAGE_VENDOR "ShellWarden")
    set(CPACK_PACKAGE_CONTACT "ShellWarden maintainers")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Secure SSH client with vault, proxy kill switch, SFTP and tunnels")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    # QML modules are loaded at run time, so shlibdeps cannot see them.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-shapes, qml6-module-qtquick-dialogs, qml6-module-qtquick-window, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript")
    set(CPACK_GENERATOR "DEB;TGZ")
    include(CPack)
endif()

# ---------------------------------------------------------------------------
# Tests: cmake -DSHELLWARDEN_BUILD_TESTS=ON, then ctest (see tests/README.md)
option(SHELLWARDEN_BUILD_TESTS "Build the unit and integration tests" OFF)
if (SHELLWARDEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
