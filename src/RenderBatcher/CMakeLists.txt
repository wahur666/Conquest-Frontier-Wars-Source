# Source files
set(SOURCES
        RenderBatcher.cpp
)

# Create shared library
add_library(RenderBatcher SHARED ${SOURCES})

# Include directories
target_include_directories(RenderBatcher PRIVATE
        .
        ..
        ../../Libs
        ../../Include
)

# Preprocessor definitions
target_compile_definitions(RenderBatcher PRIVATE
        _WINDOWS
        D3D_OVERLOADS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(RenderBatcher PRIVATE
        MathLib
        DACOM
        ddraw
        RPUL
        $<$<CONFIG:Debug>:dxguid>
)

# MSVC compiler options
target_compile_options(RenderBatcher PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)


target_include_directories(RenderBatcher PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(RenderBatcher PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)