# Source files
set(SOURCES
        DirectDraw/D3d/Direct3D_pipe.cpp
        DirectDraw/DirectDraw.cpp
        DirectDraw/DirectDrawTexture.cpp
        DirectDraw/VideoForWindows.cpp
        RenderDebugger.cpp
)

# Create shared library
add_library(D3DRenderPipe SHARED ${SOURCES})

# Include directories
target_include_directories(D3DRenderPipe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Include
)

# Preprocessor definitions
target_compile_definitions(D3DRenderPipe PRIVATE
        _WINDOWS
        D3D_OVERLOADS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8;RP_RD_DEBUG=1>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3;RP_RD_DEBUG=0>
)

# Link libraries
target_link_libraries(D3DRenderPipe PRIVATE
        vfw32
        MathLib
        DACOM
        ddraw
        RPUL
        $<$<CONFIG:Debug>:dxguid;d3dx9d;d3d9>
        $<$<CONFIG:Release>:d3dx9>
)

# MSVC compiler options
target_compile_options(D3DRenderPipe PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2;/Oi;/Oy>
)

# Linker options
target_link_options(D3DRenderPipe PRIVATE
        /FORCE:MULTIPLE
)

target_include_directories(D3DRenderPipe PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(D3DRenderPipe PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)