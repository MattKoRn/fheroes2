# FindSDL2_mixer.cmake
set(SDL2_MIXER_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/include")
set(SDL2_MIXER_LIBRARY "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/lib/x64/SDL2_mixer.lib")

if(NOT TARGET SDL2_mixer::SDL2_mixer)
    add_library(SDL2_mixer::SDL2_mixer UNKNOWN IMPORTED)
    set_target_properties(SDL2_mixer::SDL2_mixer PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/lib/x64/SDL2_mixer.dll"
        IMPORTED_IMPLIB "${SDL2_MIXER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_MIXER_INCLUDE_DIR}"
    )
endif()

set(SDL2_MIXER_FOUND TRUE)
