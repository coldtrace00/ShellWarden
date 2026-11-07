@echo off
setlocal EnableExtensions EnableDelayedExpansion
title ShellWarden - build

rem ===================================================================
rem  ShellWarden: one-click build and run for Windows 10/11.
rem  Double-click this file (or run it from a command prompt in the project folder).
rem
rem  1. finds Qt (MinGW kit), CMake, Ninja, MSYS2
rem  2. installs libssh / libsodium / libvterm into MSYS2 if missing
rem  3. builds Release into .\build
rem  4. copies all required DLLs next to ShellWarden.exe (windeployqt)
rem  5. starts the program
rem
rem  build.bat package  - additionally makes dist\ShellWarden-<ver>-portable.zip,
rem                      dist\ShellWarden-<ver>-setup.exe (if Inno Setup 6 or 7 is installed)
rem                      and dist\release\ (both + SHA256SUMS) to upload to GitHub Releases
rem
rem  Full log: build\build.log
rem  Paths can be overridden: set QT_DIR=..., MINGW_DIR=..., MSYS2_DIR=...
rem ===================================================================

cd /d "%~dp0"

if not defined QT_ROOT   set "QT_ROOT=C:\Qt"
if not defined MSYS2_DIR set "MSYS2_DIR=C:\msys64"

rem ---- Qt kit (newest C:\Qt\6.x\mingw_64) ---------------------------
if not defined QT_DIR (
    for /d %%D in ("%QT_ROOT%\6.*") do (
        if exist "%%D\mingw_64\bin\windeployqt.exe" set "QT_DIR=%%D\mingw_64"
    )
)
if not defined QT_DIR goto :no_qt
if not exist "%QT_DIR%\bin\windeployqt.exe" goto :no_qt

rem ---- MinGW, CMake, Ninja from Qt Tools -----------------------------
if not defined MINGW_DIR (
    for /d %%D in ("%QT_ROOT%\Tools\mingw*_64") do (
        if exist "%%D\bin\g++.exe" set "MINGW_DIR=%%D"
    )
)
if not defined MINGW_DIR goto :no_mingw

set "CMAKE_EXE=%QT_ROOT%\Tools\CMake_64\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    where cmake >nul 2>&1 && (set "CMAKE_EXE=cmake") || goto :no_cmake
)
set "NINJA_DIR=%QT_ROOT%\Tools\Ninja"
if not exist "%NINJA_DIR%\ninja.exe" goto :no_ninja

rem Only the Qt toolchain is on PATH during the build: MSYS2 bin folders
rem are never added, so nothing from MSYS2 can shadow the compiler.
set "PATH=%MINGW_DIR%\bin;%NINJA_DIR%;%QT_DIR%\bin;%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\WindowsPowerShell\v1.0"

echo.
echo  Qt     : %QT_DIR%
echo  MinGW  : %MINGW_DIR%
echo  CMake  : %CMAKE_EXE%
echo  MSYS2  : %MSYS2_DIR%
echo.

rem ---- MSYS2 libraries ------------------------------------------------
set "DEPS=%MSYS2_DIR%\ucrt64"
if not exist "%MSYS2_DIR%\usr\bin\bash.exe" goto :no_msys

set "NEED_DEPS="
if not exist "%DEPS%\include\libssh\libssh.h" set NEED_DEPS=1
if not exist "%DEPS%\include\sodium.h"        set NEED_DEPS=1
if not exist "%DEPS%\include\vterm.h"         set NEED_DEPS=1
if defined NEED_DEPS (
    echo [1/4] Installing libssh, libsodium, libvterm into MSYS2 UCRT64...
    set "PKGS=mingw-w64-ucrt-x86_64-libssh mingw-w64-ucrt-x86_64-libsodium mingw-w64-ucrt-x86_64-libvterm"
    "%MSYS2_DIR%\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm !PKGS!"
    if not exist "%DEPS%\include\libssh\libssh.h" (
        echo       Package database is old, refreshing it...
        "%MSYS2_DIR%\usr\bin\bash.exe" -lc "pacman -Sy --noconfirm && pacman -S --needed --noconfirm !PKGS!"
    )
    if not exist "%DEPS%\include\libssh\libssh.h" goto :deps_failed
    if not exist "%DEPS%\include\sodium.h"        goto :deps_failed
    if not exist "%DEPS%\include\vterm.h"         goto :deps_failed
) else (
    echo [1/4] Libraries already installed.
)

rem ---- configure -------------------------------------------------------
rem A build folder left from the old name (Shellwave) has stale CMake state:
rem it only contains generated files, so start it fresh.
if exist build\CMakeCache.txt (
    findstr /c:"CMAKE_PROJECT_NAME:STATIC=Shellwave" build\CMakeCache.txt >nul 2>&1 && (
        echo       Old build folder from the previous name found, rebuilding from scratch...
        rmdir /s /q build
    )
)
if not exist build mkdir build
set "LOG=%CD%\build\build.log"
echo ShellWarden build %DATE% %TIME% > "%LOG%"

set "DEPS_FWD=%DEPS:\=/%"
rem The in-app updater checks GitHub Releases of this repository.
rem Passed every time, so an old CMake cache can never leave it empty.
if not defined APP_GITHUB_REPO set "APP_GITHUB_REPO=coldtrace00/ShellWarden"
rem release builds: set APP_UPDATE_PUBKEY=RWQ... (minisign.pub) so updates must be signed
echo [2/4] Configuring (CMake)...
"%CMAKE_EXE%" -S . -B build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_PREFIX_PATH=%QT_DIR%" ^
    "-DCMAKE_MAKE_PROGRAM=%NINJA_DIR%\ninja.exe" ^
    "-DCMAKE_C_COMPILER=%MINGW_DIR%\bin\gcc.exe" ^
    "-DCMAKE_CXX_COMPILER=%MINGW_DIR%\bin\g++.exe" ^
    "-DSHELLWARDEN_DEPS_PREFIX=%DEPS_FWD%" ^
    "-DAPP_GITHUB_REPO=%APP_GITHUB_REPO%" ^
    "-DAPP_UPDATE_PUBKEY=%APP_UPDATE_PUBKEY%" >> "%LOG%" 2>&1
if errorlevel 1 goto :build_failed

rem ---- a running copy locks ShellWarden.exe and the link step fails -------
tasklist /FI "IMAGENAME eq ShellWarden.exe" 2>nul | find /I "ShellWarden.exe" >nul
if not errorlevel 1 (
    echo.
    echo  ShellWarden is running, so its .exe cannot be replaced.
    choice /C YN /M " Close it now (open sessions will be disconnected)"
    if errorlevel 2 (
        echo  Close ShellWarden yourself and run build.bat again.
        goto :wait
    )
    taskkill /IM ShellWarden.exe >nul 2>&1
    timeout /t 2 >nul
    tasklist /FI "IMAGENAME eq ShellWarden.exe" 2>nul | find /I "ShellWarden.exe" >nul
    if not errorlevel 1 taskkill /F /IM ShellWarden.exe >nul 2>&1
    timeout /t 1 >nul
)

rem ---- build -----------------------------------------------------------
echo [3/4] Compiling (first time takes a few minutes)...
"%CMAKE_EXE%" --build build --parallel >> "%LOG%" 2>&1
if errorlevel 1 goto :build_failed
if not exist "build\ShellWarden.exe" goto :build_failed

rem ---- deploy Qt runtime ----------------------------------------------
echo [4/4] Copying Qt runtime next to ShellWarden.exe...
"%QT_DIR%\bin\windeployqt.exe" --release --qmldir qml "build\ShellWarden.exe" >> "%LOG%" 2>&1
if errorlevel 1 goto :build_failed

if /i "%~1"=="package" goto :package

echo.
echo  Done:  %CD%\build\ShellWarden.exe
echo  Next time you can run that .exe directly (or run build.bat again after changes).
echo.
start "" "%CD%\build\ShellWarden.exe"
timeout /t 4 >nul
exit /b 0

rem =====================================================================
:package
rem Clean copy with only what the program needs: the exe, the MSYS2 DLLs that
rem CMake copied next to it, then windeployqt adds Qt.
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "(Select-String -Path CMakeLists.txt -Pattern 'project\(ShellWarden VERSION ([0-9.]+)').Matches[0].Groups[1].Value"`) do set "APPVER=%%V"
if not defined APPVER set "APPVER=0.0.0"
echo [5/5] Packaging ShellWarden %APPVER%...
if exist dist\ShellWarden rmdir /s /q dist\ShellWarden
mkdir dist\ShellWarden
copy /y "build\ShellWarden.exe" dist\ShellWarden\ >nul
copy /y "build\*.dll" dist\ShellWarden\ >nul
"%QT_DIR%\bin\windeployqt.exe" --release --qmldir qml --no-translations "dist\ShellWarden\ShellWarden.exe" >> "%LOG%" 2>&1
if errorlevel 1 goto :build_failed
copy /y LICENSE dist\ShellWarden\LICENSE.txt >nul

rem Portable: the same files plus an empty "portable" marker -> data stays in .\data
if exist dist\portable rmdir /s /q dist\portable
mkdir dist\portable
xcopy /e /i /q /y dist\ShellWarden dist\portable\ShellWarden >nul
type nul > dist\portable\ShellWarden\portable
set "ZIP=%CD%\dist\ShellWarden-%APPVER%-portable.zip"
if exist "%ZIP%" del "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path 'dist\portable\ShellWarden' -DestinationPath '%ZIP%'" >> "%LOG%" 2>&1
rmdir /s /q dist\portable
if exist "%ZIP%" (echo        Portable: %ZIP%) else (echo        Portable zip FAILED, see build\build.log)

rem Inno Setup 6 or 7 (the newest found wins), installed for everyone or just this user
set "ISCC="
for %%V in (6 7) do (
    if exist "%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe"
    if exist "%ProgramFiles%\Inno Setup %%V\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup %%V\ISCC.exe"
    if exist "%LOCALAPPDATA%\Programs\Inno Setup %%V\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup %%V\ISCC.exe"
)
if not defined ISCC for /f "delims=" %%I in ('where ISCC.exe 2^>nul') do if not defined ISCC set "ISCC=%%I"
if defined ISCC (
    "!ISCC!" /Q "/DAppVersion=%APPVER%" "/DSourceDir=%CD%\dist\ShellWarden" "/DOutputDir=%CD%\dist" packaging\windows\shellwarden.iss >> "%LOG%" 2>&1
    if exist "dist\ShellWarden-%APPVER%-setup.exe" (echo        Installer: %CD%\dist\ShellWarden-%APPVER%-setup.exe) else (echo        Installer FAILED, see build\build.log)
) else (
    echo        Installer skipped: Inno Setup 6/7 is not installed ^(https://jrsoftware.org/isdl.php^).
)

rem Release folder: exactly the files to attach to a GitHub release.
rem SHA256SUMS is required by the in-app updater (LF line endings, no BOM).
if exist dist\release rmdir /s /q dist\release
mkdir dist\release
if exist "dist\ShellWarden-%APPVER%-setup.exe" copy /y "dist\ShellWarden-%APPVER%-setup.exe" dist\release\ >nul
if exist "%ZIP%" copy /y "%ZIP%" dist\release\ >nul
powershell -NoProfile -Command "$l = Get-ChildItem -File 'dist\release' | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name }; [IO.File]::WriteAllText('%CD%\dist\release\SHA256SUMS', (($l -join [char]10) + [char]10))" >> "%LOG%" 2>&1
if exist dist\release\SHA256SUMS (echo        Release:   %CD%\dist\release  ^(upload all files to the GitHub release^)) else (echo        SHA256SUMS FAILED, see build\build.log)
if defined APP_UPDATE_PUBKEY echo        Signed build: run tools\sign_release.ps1 -Folder dist\release before uploading.
echo.
echo  Done. Files are in %CD%\dist
start "" "%CD%\dist"
timeout /t 4 >nul
exit /b 0

rem =====================================================================
:build_failed
echo.
echo  *** BUILD FAILED ***  Last lines of build\build.log:
echo  ---------------------------------------------------------------
powershell -NoProfile -Command "Get-Content -Tail 40 '%LOG%'"
echo  ---------------------------------------------------------------
echo  Send build\build.log (or a screenshot of this window) to fix it.
goto :wait

:no_qt
echo  Qt MinGW kit not found in %QT_ROOT%\6.*\mingw_64.
echo  Set it manually, e.g.:  set QT_DIR=C:\Qt\6.12.0\mingw_64
goto :wait
:no_mingw
echo  MinGW not found in %QT_ROOT%\Tools\mingw*_64. Install it with Qt Maintenance Tool.
goto :wait
:no_cmake
echo  CMake not found. Install "CMake" in Qt Maintenance Tool (Developer and Designer Tools).
goto :wait
:no_ninja
echo  Ninja not found. Install "Ninja" in Qt Maintenance Tool (Developer and Designer Tools).
goto :wait
:no_msys
echo  MSYS2 not found in %MSYS2_DIR%. Set it:  set MSYS2_DIR=D:\msys64
goto :wait
:deps_failed
echo  Could not install the libraries. Open "MSYS2 UCRT64" and run:
echo    pacman -S --needed mingw-w64-ucrt-x86_64-libssh mingw-w64-ucrt-x86_64-libsodium mingw-w64-ucrt-x86_64-libvterm
echo  then run build.bat again.
goto :wait

:wait
echo.
pause
exit /b 1
