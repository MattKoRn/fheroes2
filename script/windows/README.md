# Windows helper scripts

## One-click build and install

Double-click `build_install_and_shortcut.bat`.

The helper is designed to work on a normal Windows 10/11 machine without Git, CMake, vcpkg, or the Visual Studio C++ Build Tools already installed.

It automatically:

- checks for Windows Package Manager (`winget`),
- installs **Git for Windows** if Git is missing,
- installs **CMake** if CMake is missing,
- installs **Visual Studio 2022 Build Tools** with the C++ workload when no compatible compiler is found,
- refreshes `PATH` inside the same script so newly installed Git/CMake can be used immediately,
- bootstraps a private vcpkg checkout under `%LOCALAPPDATA%\fheroes2-vcpkg`,
- installs the required x64 SDL2/SDL2_image/SDL2_mixer/zlib dependencies,
- builds a Release version,
- installs it to `%LOCALAPPDATA%\Programs\fheroes2`,
- copies the runtime DLLs, and
- creates or refreshes `fheroes2.lnk` on the current user's Desktop.

### Only prerequisite

Windows Package Manager (`winget`) must be available. It is normally provided by Microsoft's **App Installer** on Windows 10/11. If the script says winget is missing, install or update **App Installer** from the Microsoft Store and run the batch file again.

Windows may ask for administrator approval when the Visual Studio C++ Build Tools need to be installed.

The original `install_packages.bat` helper remains available for the older Visual Studio package workflow.
