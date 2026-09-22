@echo off
setlocal EnableExtensions EnableDelayedExpansion

title fheroes2 - Build, Install and Create Desktop Shortcut

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\build\windows-release"
set "INSTALL_DIR=%LOCALAPPDATA%\Programs\fheroes2"
set "TRIPLET=x64-windows"

echo.
echo ============================================================
echo  fheroes2 one-click Release build and install
echo ============================================================
echo.

where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git was not found in PATH.
    echo Install Git for Windows, then run this file again.
    goto :fail
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    echo Install CMake and enable "Add CMake to PATH", then run this file again.
    goto :fail
)

if not defined VCPKG_ROOT (
    set "VCPKG_ROOT=%LOCALAPPDATA%\fheroes2-vcpkg"
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [1/6] Preparing vcpkg in "%VCPKG_ROOT%"...
    if not exist "%VCPKG_ROOT%\.git" (
        git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
        if errorlevel 1 goto :fail
    )

    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 goto :fail
) else (
    echo [1/6] Using vcpkg at "%VCPKG_ROOT%".
)

echo [2/6] Installing build dependencies...
"%VCPKG_ROOT%\vcpkg.exe" install --triplet %TRIPLET% sdl2 sdl2-image sdl2-mixer zlib
if errorlevel 1 goto :fail

echo [3/6] Configuring Release build...
cmake -S "%ROOT%" -B "%BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
    -DENABLE_IMAGE=ON ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%"
if errorlevel 1 goto :fail

echo [4/6] Compiling fheroes2...
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 goto :fail

echo [5/6] Installing to "%INSTALL_DIR%"...
cmake --install "%BUILD_DIR%" --config Release
if errorlevel 1 goto :fail

if exist "%BUILD_DIR%\vcpkg_installed\%TRIPLET%\bin\*.dll" (
    copy /Y "%BUILD_DIR%\vcpkg_installed\%TRIPLET%\bin\*.dll" "%INSTALL_DIR%\bin\" >nul
)

if not exist "%INSTALL_DIR%\bin\fheroes2.exe" (
    echo ERROR: Installation completed but "%INSTALL_DIR%\bin\fheroes2.exe" was not found.
    goto :fail
)

echo [6/6] Creating Desktop shortcut...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$desktop=[Environment]::GetFolderPath('Desktop');" ^
    "$shell=New-Object -ComObject WScript.Shell;" ^
    "$shortcut=$shell.CreateShortcut((Join-Path $desktop 'fheroes2.lnk'));" ^
    "$shortcut.TargetPath='%INSTALL_DIR%\bin\fheroes2.exe';" ^
    "$shortcut.WorkingDirectory='%INSTALL_DIR%\bin';" ^
    "$shortcut.IconLocation='%INSTALL_DIR%\bin\fheroes2.exe,0';" ^
    "$shortcut.Save()"
if errorlevel 1 goto :fail

echo.
echo SUCCESS: fheroes2 is installed at:
echo   %INSTALL_DIR%
echo.
echo A Desktop shortcut named "fheroes2" has been created.
echo.
pause
exit /b 0

:fail
echo.
echo BUILD OR INSTALL FAILED.
echo Review the error above. Visual Studio 2019 or newer with the
echo "Desktop development with C++" workload is required.
echo.
pause
exit /b 1
