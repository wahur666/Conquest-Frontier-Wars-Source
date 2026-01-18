# Source files
set(SOURCES
        TextureLibrary.cpp
)

# Create shared library
add_library(TextureLibrary SHARED ${SOURCES})

# Include directories
target_include_directories(TextureLibrary PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../Include
)

# Preprocessor definitions
target_compile_definitions(TextureLibrary PRIVATE
        _WINDOWS
        D3D_OVERLOADS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(TextureLibrary PRIVATE
        winmm
        MathLib
        DACOM
        ddraw
        RPUL
        $<$<CONFIG:Debug>:dxguid>
)


# MSVC compiler options
target_compile_options(TextureLibrary PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2;/W4>
)

# Linker options
target_link_options(TextureLibrary PRIVATE
        /FORCE:MULTIPLE
)


target_include_directories(TextureLibrary PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(TextureLibrary PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)