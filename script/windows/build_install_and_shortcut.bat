@echo off
setlocal EnableExtensions EnableDelayedExpansion

title fheroes2 - One-click Build and Install

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\build\windows-release"
set "INSTALL_DIR=%LOCALAPPDATA%\Programs\fheroes2"
set "TRIPLET=x64-windows"
set "VCPKG_DEFAULT_BINARY_CACHE=%LOCALAPPDATA%\fheroes2-vcpkg-cache"
set "PATH=%PATH%;%LOCALAPPDATA%\Microsoft\WindowsApps"

if not exist "%VCPKG_DEFAULT_BINARY_CACHE%" (
    mkdir "%VCPKG_DEFAULT_BINARY_CACHE%"
    if errorlevel 1 (
        echo ERROR: Could not create the vcpkg binary cache directory:
        echo   %VCPKG_DEFAULT_BINARY_CACHE%
        goto :fail
    )
)

echo.
echo ============================================================
echo  fheroes2 one-click Release build and install
echo ============================================================
echo.
echo Missing Windows build tools will be installed automatically
echo with Windows Package Manager (winget).
echo.

call :select_game_data
if errorlevel 1 goto :fail

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
    echo [5/11] Preparing vcpkg in "%VCPKG_ROOT%"...
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
    echo [5/11] Using existing vcpkg at "%VCPKG_ROOT%".
)

echo.
echo [6/11] Installing SDL2 and zlib dependencies...
"%VCPKG_ROOT%\vcpkg.exe" install --triplet %TRIPLET% sdl2 sdl2-image sdl2-mixer zlib
if errorlevel 1 (
    echo ERROR: vcpkg dependency installation failed.
    goto :fail
)

echo.
echo [7/11] Cleaning old CMake build cache...
if exist "%BUILD_DIR%" (
    rmdir /S /Q "%BUILD_DIR%"
    if exist "%BUILD_DIR%" (
        echo ERROR: Could not remove the old build directory:
        echo   %BUILD_DIR%
        echo Close Visual Studio or any program using files in that folder, then run this script again.
        goto :fail
    )
)

echo.
echo [8/11] Configuring Release build...
cmake -S "%ROOT%" -B "%BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
    -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON ^
    -DENABLE_IMAGE=ON ^
    -DGET_HOMM2_DEMO=%GET_HOMM2_DEMO% ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%"
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    goto :fail
)

echo.
echo [9/11] Compiling fheroes2...
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 (
    echo ERROR: Compilation failed.
    goto :fail
)

echo.
echo [10/11] Installing to "%INSTALL_DIR%"...
cmake --install "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo ERROR: CMake install failed.
    goto :fail
)

if not exist "%INSTALL_DIR%\bin" mkdir "%INSTALL_DIR%\bin"

if exist "%VCPKG_ROOT%\installed\%TRIPLET%\bin\*.dll" (
    copy /Y "%VCPKG_ROOT%\installed\%TRIPLET%\bin\*.dll" "%INSTALL_DIR%\bin\" >nul
) else if exist "%BUILD_DIR%\vcpkg_installed\%TRIPLET%\bin\*.dll" (
    rem Manifest-mode fallback, in case the project switches to it later.
    copy /Y "%BUILD_DIR%\vcpkg_installed\%TRIPLET%\bin\*.dll" "%INSTALL_DIR%\bin\" >nul
) else (
    echo WARNING: No vcpkg runtime DLL directory was found.
)

if not exist "%INSTALL_DIR%\bin\fheroes2.exe" (
    echo ERROR: Installation completed but "%INSTALL_DIR%\bin\fheroes2.exe" was not found.
    goto :fail
)

if "%GET_HOMM2_DEMO%"=="OFF" (
    call :install_full_game_data
    if errorlevel 1 goto :fail
)

if not exist "%INSTALL_DIR%\share\fheroes2\data\HEROES2.AGG" (
    echo ERROR: The game executable was installed, but the required HoMM II game data was not.
    echo Expected:
    echo   %INSTALL_DIR%\share\fheroes2\data\HEROES2.AGG
    echo.
    if "%GET_HOMM2_DEMO%"=="ON" (
        echo The free demo was selected, but its data did not install correctly.
        echo Review the CMake download/install messages above.
    ) else (
        echo Full Heroes II data was selected, but HEROES2.AGG was not copied correctly.
        echo Verify the folder you selected contains DATA\HEROES2.AGG.
    )
    goto :fail
)

echo.
echo [11/11] Creating Desktop shortcut...
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
if "%GET_HOMM2_DEMO%"=="ON" (
    echo HoMM II demo game data was installed.
) else (
    echo Full HoMM II game data was imported from:
    echo   %HOMM2_DIR%
)
echo.
pause
exit /b 0

:select_game_data
echo ============================================================
echo  Heroes II game data
echo ============================================================
echo.
echo fheroes2 needs data from Heroes of Might and Magic II.
echo.
echo [1] Use my full version of Heroes II
echo [2] Use the free Heroes II demo
echo.
choice /C 12 /N /M "Choose 1 or 2: "
if errorlevel 2 (
    set "GET_HOMM2_DEMO=ON"
    set "HOMM2_DIR="
    echo.
    echo The free demo will be downloaded during the build.
    echo.
    exit /b 0
)

set "GET_HOMM2_DEMO=OFF"
echo.
echo Enter the folder where the full version of Heroes II is installed.
echo Example: C:\GOG Games\HoMM 2 Gold
echo.
set /p "HOMM2_DIR=Full Heroes II folder: "

if not defined HOMM2_DIR (
    echo ERROR: No Heroes II folder was entered.
    exit /b 1
)

for %%I in ("%HOMM2_DIR%") do set "HOMM2_DIR=%%~fI"

if not exist "%HOMM2_DIR%\DATA\HEROES2.AGG" (
    echo.
    echo ERROR: This does not appear to be a full Heroes II installation.
    echo The installer could not find:
    echo   %HOMM2_DIR%\DATA\HEROES2.AGG
    echo.
    echo Run the installer again and select the folder containing DATA\HEROES2.AGG.
    exit /b 1
)

echo.
echo Full Heroes II data found:
echo   %HOMM2_DIR%
echo.
exit /b 0

:install_full_game_data
echo.
echo Importing full Heroes II game data...
set "GAME_DATA_DIR=%INSTALL_DIR%\share\fheroes2"

if not exist "%GAME_DATA_DIR%" mkdir "%GAME_DATA_DIR%"

call :copy_game_data_dir "%HOMM2_DIR%\DATA" "%GAME_DATA_DIR%\data" "DATA"
if errorlevel 1 exit /b 1

call :copy_game_data_dir "%HOMM2_DIR%\MAPS" "%GAME_DATA_DIR%\maps" "MAPS"
if errorlevel 1 exit /b 1

if exist "%HOMM2_DIR%\MUSIC" (
    call :copy_game_data_dir "%HOMM2_DIR%\MUSIC" "%GAME_DATA_DIR%\music" "MUSIC"
    if errorlevel 1 exit /b 1
)

if exist "%HOMM2_DIR%\ANIM" (
    call :copy_game_data_dir "%HOMM2_DIR%\ANIM" "%GAME_DATA_DIR%\anim" "ANIM"
    if errorlevel 1 exit /b 1
) else if exist "%HOMM2_DIR%\HEROES2\ANIM" (
    call :copy_game_data_dir "%HOMM2_DIR%\HEROES2\ANIM" "%GAME_DATA_DIR%\anim" "ANIM"
    if errorlevel 1 exit /b 1
)

if not exist "%GAME_DATA_DIR%\data\HEROES2.AGG" (
    echo ERROR: HEROES2.AGG was not copied into the fheroes2 data directory.
    exit /b 1
)

echo Full Heroes II resources imported successfully.
exit /b 0

:copy_game_data_dir
set "SOURCE_DIR=%~1"
set "DEST_DIR=%~2"
set "DATA_LABEL=%~3"

if not exist "%SOURCE_DIR%" (
    if /I "%DATA_LABEL%"=="MAPS" (
        echo ERROR: Required Heroes II MAPS directory was not found:
        echo   %SOURCE_DIR%
        exit /b 1
    )
    exit /b 0
)

if not exist "%DEST_DIR%" mkdir "%DEST_DIR%"

robocopy "%SOURCE_DIR%" "%DEST_DIR%" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo ERROR: Failed to copy Heroes II %DATA_LABEL% files.
    exit /b 1
)

exit /b 0

:ensure_winget
where winget >nul 2>nul
if not errorlevel 1 (
    echo [1/11] Windows Package Manager found.
    exit /b 0
)

echo [1/11] ERROR: Windows Package Manager (winget) was not found.
echo.
echo Install or update "App Installer" from the Microsoft Store,
echo then double-click this file again. Git and CMake do not need
echo to be installed manually.
echo.
exit /b 1

:refresh_path
rem winget's Git and CMake packages normally install to these locations.
rem Adding them directly lets this same CMD process use newly installed tools.
set "PATH=%PATH%;%ProgramFiles%\Git\cmd;%ProgramFiles%\CMake\bin;%LOCALAPPDATA%\Programs\Git\cmd;%LOCALAPPDATA%\Programs\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;%LOCALAPPDATA%\Microsoft\WindowsApps"
exit /b 0

:ensure_git
where git >nul 2>nul
if not errorlevel 1 (
    echo [2/11] Git found.
    exit /b 0
)

echo [2/11] Git is missing. Installing Git for Windows...
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
    echo [3/11] CMake found.
    exit /b 0
)

echo [3/11] CMake is missing. Installing CMake...
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
    echo [4/11] Visual Studio C++ Build Tools found.
    exit /b 0
)

echo [4/11] Visual Studio C++ Build Tools are missing.
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
echo vcpkg, Visual Studio C++ Build Tools, game data import/download,
echo compilation, installation, and Desktop shortcut creation automatically.
echo.
pause
exit /b 1
