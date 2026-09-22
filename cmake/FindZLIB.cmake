# FindZLIB.cmake
set(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/include")
set(ZLIB_LIBRARY "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/lib/x64/zlib.lib")

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/VisualStudio/packages/sdl2/lib/x64/zlib1.dll"
        IMPORTED_IMPLIB "${ZLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIR}"
    )
endif()

set(ZLIB_FOUND TRUE)
