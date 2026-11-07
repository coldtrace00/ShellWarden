#!/usr/bin/env bash
# ShellWarden: build and run on Linux.
#
#   ./build.sh            build into ./build-linux and start the program
#   ./build.sh package    also make build-linux/shellwarden_<ver>_amd64.deb and a .tar.gz
#   ./build.sh appimage   also make build-linux/ShellWarden-<ver>-x86_64.AppImage (one file, any distro;
#                         downloads linuxdeploy + its Qt plugin into build-linux/tools the first time)
#   ./build.sh deps       only print / install the packages it needs
#
# Needs Qt 6.4+ (system packages or the Qt online installer: set QT_DIR=~/Qt/6.x/gcc_64),
# libssh, libsodium, libvterm, OpenSSL, CMake and a C++17 compiler.
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR=${BUILD_DIR:-build-linux}

deb_pkgs="build-essential cmake ninja-build pkg-config qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools qt6-serialport-dev libssh-dev libsodium-dev libvterm-dev libssl-dev qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-shapes qml6-module-qtquick-dialogs qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtqml-workerscript"
fedora_pkgs="gcc-c++ cmake ninja-build pkgconf qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qttools-devel qt6-qtserialport-devel qt6-qtshapes libssh-devel libsodium-devel libvterm-devel openssl-devel"
arch_pkgs="base-devel cmake ninja pkgconf qt6-base qt6-declarative qt6-tools qt6-serialport qt6-shapes libssh libsodium libvterm openssl"

missing_deps() {
    for m in libssh libsodium vterm libcrypto; do
        pkg-config --exists "$m" 2>/dev/null || return 0
    done
    command -v cmake >/dev/null || return 0
    return 1
}

install_deps() {
    echo "Installing build dependencies (sudo)…"
    if command -v apt-get >/dev/null; then
        sudo apt-get update && sudo apt-get install -y $deb_pkgs
    elif command -v dnf >/dev/null; then
        sudo dnf install -y $fedora_pkgs
    elif command -v pacman >/dev/null; then
        sudo pacman -S --needed --noconfirm $arch_pkgs
    else
        echo "Unknown distribution. Install: Qt 6 (base, declarative, tools, serialport), libssh, libsodium, libvterm, OpenSSL, CMake, Ninja."
        exit 1
    fi
}

if [[ "${1:-}" == "deps" ]]; then
    install_deps
    exit 0
fi

if missing_deps; then
    echo "Some libraries are missing."
    read -r -p "Install them now with your package manager? [Y/n] " a
    [[ "${a:-y}" =~ ^[Yy]$ ]] && install_deps || { echo "Run ./build.sh deps later."; exit 1; }
fi

gen=()
command -v ninja >/dev/null && gen=(-G Ninja)
prefix=()
[[ -n "${QT_DIR:-}" ]] && prefix=("-DCMAKE_PREFIX_PATH=$QT_DIR")

echo "[1/2] Configuring…"
# the in-app updater checks GitHub Releases of this repository (passed every time,
# so an old CMake cache can never leave it empty)
cmake -S . -B "$BUILD_DIR" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr "${prefix[@]}" \
      "-DAPP_GITHUB_REPO=${APP_GITHUB_REPO:-coldtrace00/ShellWarden}" "-DAPP_UPDATE_PUBKEY=${APP_UPDATE_PUBKEY:-}"
echo "[2/2] Compiling…"
cmake --build "$BUILD_DIR" --parallel

if [[ "${1:-}" == "appimage" ]]; then
    ver=$(sed -n 's/^project(ShellWarden VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
    root=$PWD
    build=$(cd "$BUILD_DIR" && pwd)
    tools="$build/tools"; appdir="$build/AppDir"
    mkdir -p "$tools"
    for t in linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage; do
        if [[ ! -x "$tools/$t" ]]; then
            echo "Downloading $t…"
            case $t in
                linuxdeploy-x86_64.AppImage) url=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$t ;;
                *) url=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/$t ;;
            esac
            curl -fL --retry 3 -o "$tools/$t" "$url"
            chmod +x "$tools/$t"
        fi
    done
    rm -rf "$appdir"
    DESTDIR="$appdir" cmake --install "$BUILD_DIR"
    # Qt: QML modules used by qml/, Wayland + X11, SVG icons; qmake6 tells the plugin where Qt is
    export QML_SOURCES_PATHS="$root/qml"
    export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqxcb.so"
    if [[ -n "${QT_DIR:-}" ]]; then export QMAKE="$QT_DIR/bin/qmake"; else export QMAKE=$(command -v qmake6 || command -v qmake); fi
    export OUTPUT="ShellWarden-$ver-x86_64.AppImage"
    export APPIMAGE_EXTRACT_AND_RUN=1   # works without FUSE (containers, CI)
    (cd "$build" && "$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
        --desktop-file "$appdir/usr/share/applications/shellwarden.desktop" \
        --icon-file "$root/branding/icon.png" --icon-filename shellwarden \
        --plugin qt --output appimage)
    echo
    echo "Done: $build/$OUTPUT"
    echo "Run it: $build/$OUTPUT   (the in-app updater replaces this file itself)"
    exit 0
fi

if [[ "${1:-}" == "package" ]]; then
    (cd "$BUILD_DIR" && cpack -G "DEB;TGZ")
    echo
    ls -1 "$BUILD_DIR"/*.deb "$BUILD_DIR"/*.tar.gz 2>/dev/null
    deb=$(ls "$BUILD_DIR"/*.deb | head -1); [[ $deb == /* ]] || deb="./$deb"
    echo "Install: sudo apt install $deb"
    exit 0
fi

echo
bin="$BUILD_DIR/shellwarden"; [[ $bin == /* ]] || bin="./$bin"
echo "Done: $bin"
echo "Portable mode: create an empty file named 'portable' next to the binary."
exec "$bin"
