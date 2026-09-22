@echo off
setlocal EnableExtensions EnableDelayedExpansion

title fheroes2 - One-click Build and Install

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\build\windows-release"
set "INSTALL_DIR=%LOCALAPPDATA%\Programs\fheroes2"
set "TRIPLET=x64-windows"
set "VCPKG_DEFAULT_BINARY_CACHE=%LOCALAPPDATA%\fheroes2-vcpkg-cache"

echo.
echo ============================================================
echo  fheroes2 one-click Release build and install
echo ============================================================
echo.
echo Missing Windows build tools will be installed automatically
echo with Windows Package Manager (winget).
echo.

call :ensure_winget
if errorlevel 1 goto :fail

call :ensure_git
if errorlevel 1 goto :fail

call :ensure_cmake
if errorlevel 1 goto :fail

call :ensure_cpp_build_tools
if errorlevel 1 goto :fail

if not defined VCPKG_ROOT (
    set "VCPKG_ROOT=%LOCALAPPDATA%\fheroes2-vcpkg"
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo.
    echo [5/10] Preparing vcpkg in "%VCPKG_ROOT%"...
    if not exist "%VCPKG_ROOT%\.git" (
        git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
        if errorlevel 1 (
            echo ERROR: Could not download vcpkg.
            goto :fail
        )
    )

    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 (
        echo ERROR: vcpkg bootstrap failed.
        goto :fail
    )
) else (
    echo.
    echo [5/10] Using existing vcpkg at "%VCPKG_ROOT%".
)

echo.
echo [6/10] Installing SDL2 and zlib dependencies...
"%VCPKG_ROOT%\vcpkg.exe" install --triplet %TRIPLET% sdl2 sdl2-image sdl2-mixer zlib
if errorlevel 1 (
    echo ERROR: vcpkg dependency installation failed.
    goto :fail
)

echo.
echo [7/10] Configuring Release build...
cmake -S "%ROOT%" -B "%BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
    -DENABLE_IMAGE=ON ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%"
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    goto :fail
)

echo.
echo [8/10] Compiling fheroes2...
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 (
    echo ERROR: Compilation failed.
    goto :fail
)

echo.
echo [9/10] Installing to "%INSTALL_DIR%"...
cmake --install "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo ERROR: CMake install failed.
    goto :fail
)

if exist "%BUILD_DIR%\vcpkg_installed\%TRIPLET%\bin\*.dll" (
    if not exist "%INSTALL_DIR%\bin" mkdir "%INSTALL_DIR%\bin"
    copy /Y "%BUILD_DIR%\vcpkg_installed\%TRIPLET%\bin\*.dll" "%INSTALL_DIR%\bin\" >nul
)

if not exist "%INSTALL_DIR%\bin\fheroes2.exe" (
    echo ERROR: Installation completed but "%INSTALL_DIR%\bin\fheroes2.exe" was not found.
    goto :fail
)

echo.
echo [10/10] Creating Desktop shortcut...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$desktop=[Environment]::GetFolderPath('Desktop');" ^
    "$shell=New-Object -ComObject WScript.Shell;" ^
    "$shortcut=$shell.CreateShortcut((Join-Path $desktop 'fheroes2.lnk'));" ^
    "$shortcut.TargetPath='%INSTALL_DIR%\bin\fheroes2.exe';" ^
    "$shortcut.WorkingDirectory='%INSTALL_DIR%\bin';" ^
    "$shortcut.IconLocation='%INSTALL_DIR%\bin\fheroes2.exe,0';" ^
    "$shortcut.Save()"
if errorlevel 1 (
    echo ERROR: The game installed, but the Desktop shortcut could not be created.
    goto :fail
)

echo.
echo ============================================================
echo  SUCCESS
echo ============================================================
echo fheroes2 is installed at:
echo   %INSTALL_DIR%
echo.
echo A Desktop shortcut named "fheroes2" has been created.
echo.
pause
exit /b 0

:ensure_winget
where winget >nul 2>nul
if not errorlevel 1 (
    echo [1/10] Windows Package Manager found.
    exit /b 0
)

echo [1/10] ERROR: Windows Package Manager (winget) was not found.
echo.
echo Install or update "App Installer" from the Microsoft Store,
echo then double-click this file again. Git and CMake do not need
echo to be installed manually.
echo.
exit /b 1

:refresh_path
for /f "delims=" %%P in ('powershell -NoProfile -Command "$m=[Environment]::GetEnvironmentVariable(''Path'',''Machine''); $u=[Environment]::GetEnvironmentVariable(''Path'',''User''); Write-Output ($m+'';''+$u)"') do set "PATH=%%P"
set "PATH=%PATH%;%ProgramFiles%\Git\cmd;%ProgramFiles%\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links"
exit /b 0

:ensure_git
where git >nul 2>nul
if not errorlevel 1 (
    echo [2/10] Git found.
    exit /b 0
)

echo [2/10] Git is missing. Installing Git for Windows...
winget install --id Git.Git --exact --source winget --accept-source-agreements --accept-package-agreements --silent
if errorlevel 1 (
    echo ERROR: winget could not install Git.
    exit /b 1
)

call :refresh_path
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git installed but is still not available to this script.
    echo Restart Windows and run this file again.
    exit /b 1
)

git --version
exit /b 0

:ensure_cmake
where cmake >nul 2>nul
if not errorlevel 1 (
    echo [3/10] CMake found.
    exit /b 0
)

echo [3/10] CMake is missing. Installing CMake...
winget install --id Kitware.CMake --exact --source winget --accept-source-agreements --accept-package-agreements --silent
if errorlevel 1 (
    echo ERROR: winget could not install CMake.
    exit /b 1
)

call :refresh_path
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake installed but is still not available to this script.
    echo Restart Windows and run this file again.
    exit /b 1
)

cmake --version
exit /b 0

:ensure_cpp_build_tools
set "VS_PATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\fheroes2_vs_path.txt"
    set /p VS_PATH=<"%TEMP%\fheroes2_vs_path.txt"
    del /q "%TEMP%\fheroes2_vs_path.txt" >nul 2>nul
)

if defined VS_PATH (
    echo [4/10] Visual Studio C++ Build Tools found.
    exit /b 0
)

echo [4/10] Visual Studio C++ Build Tools are missing.
echo Windows may ask for administrator approval.
echo Installing Visual Studio 2022 Build Tools with the C++ workload...
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget ^
    --accept-source-agreements --accept-package-agreements ^
    --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
if errorlevel 1 (
    echo ERROR: winget could not install the Visual Studio C++ Build Tools.
    exit /b 1
)

set "VS_PATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\fheroes2_vs_path.txt"
    set /p VS_PATH=<"%TEMP%\fheroes2_vs_path.txt"
    del /q "%TEMP%\fheroes2_vs_path.txt" >nul 2>nul
)

if not defined VS_PATH (
    echo ERROR: The Visual C++ compiler workload was not found after installation.
    echo Restart Windows and run this file again.
    exit /b 1
)

echo Visual Studio C++ Build Tools installed successfully.
exit /b 0

:fail
echo.
echo ============================================================
echo  BUILD OR INSTALL FAILED
echo ============================================================
echo Review the specific error above.
echo.
echo If winget itself is missing, install or update "App Installer"
echo from the Microsoft Store. The script handles Git, CMake,
echo vcpkg, Visual Studio C++ Build Tools, compilation, installation,
echo and Desktop shortcut creation automatically.
echo.
pause
exit /b 1
