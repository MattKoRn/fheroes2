# FindSDL2.cmake
set(SDL2_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/include" "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/include/SDL2")
set(SDL2_LIBRARY "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/lib/x64/SDL2.lib")
set(SDL2MAIN_LIBRARY "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/lib/x64/SDL2main.lib")

if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/lib/x64/SDL2.dll"
        IMPORTED_IMPLIB "${SDL2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
    )
endif()

if(NOT TARGET SDL2::SDL2main)
    add_library(SDL2::SDL2main UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2main PROPERTIES
        IMPORTED_LOCATION "${SDL2MAIN_LIBRARY}"
        IMPORTED_IMPLIB "${SDL2MAIN_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
    )
endif()

set(SDL2_FOUND TRUE)
