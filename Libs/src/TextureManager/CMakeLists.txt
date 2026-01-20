# Source files
set(SOURCES
        BmpRead.cpp
        stdafx.cpp
        TextureManager.cpp
        Tgaread.cpp
        VfxRead.cpp
)

# Create shared library (DLL)
add_library(TextureManager SHARED ${SOURCES})

# Include directories
target_include_directories(TextureManager PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Include
)

# Preprocessor definitions
target_compile_definitions(TextureManager PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)

# Link libraries
target_link_libraries(TextureManager PRIVATE
        DACOM
        MathLib
        $<$<CONFIG:Release>:opends60.lib>
)


# Compiler options
target_compile_options(TextureManager PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Od /RTC1>
        $<$<CONFIG:Release>:/O2 /Ob1 /Oi>
)

# Linker options
set_target_properties(TextureManager PROPERTIES
        LINK_FLAGS "/FORCE:MULTIPLE"
)

target_include_directories(TextureManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(TextureManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)