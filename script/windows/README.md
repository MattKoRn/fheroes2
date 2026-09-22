# Windows helper scripts

## One-click build and install

Double-click `build_install_and_shortcut.bat`.

The script:

- bootstraps a private vcpkg checkout under `%LOCALAPPDATA%\fheroes2-vcpkg` when `VCPKG_ROOT` is not already set,
- installs the required x64 SDL2/SDL2_image/SDL2_mixer/zlib dependencies,
- configures and builds a Release version with CMake,
- installs it to `%LOCALAPPDATA%\Programs\fheroes2`,
- copies the required runtime DLLs, and
- creates or refreshes `fheroes2.lnk` on the current user's Desktop.

Prerequisites are Git, CMake, and Visual Studio 2019 or newer with the **Desktop development with C++** workload.

The original `install_packages.bat` helper remains available for the Visual Studio package workflow.
