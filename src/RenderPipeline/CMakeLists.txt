# Source files
set(SOURCES
        DirectDraw/D3D/Direct3D_pipe.cpp
        DirectDraw/DirectDraw.cpp
        DirectDraw/DirectDrawTexture.cpp
        DirectDraw/VideoForWindows.cpp
        RenderDebugger.cpp
)

# Create shared library
add_library(D3DRenderPipe SHARED ${SOURCES})

# Include directories
target_include_directories(D3DRenderPipe PRIVATE
        .
        ..
        ../../Libs
        ../../Include
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
        /Zi                    # Generate debug info (kept for Release too)
        # Debug-specific flags
        $<$<CONFIG:Debug>:
        /O0                # No optimization (critical for debugging)
        /RTC1              # Runtime error checks (stack frames, uninit vars)
        /GS                # Buffer security check
        /Gm-               # Disable incremental compilation (can cause issues)
        /fp:precise        # Precise floating point (no fast math in debug)
        /guard:cf          # Control flow guard (catch vtable corruption)
        >
        # Release-specific flags
        $<$<CONFIG:Release>:
        /O2                # Full optimization
        /Oi                # Inline intrinsics
        /Oy-               # Don't omit frame pointers (keeps stack traces valid)
        /fp:fast           # Fast floating point
        >
)

# Linker options
target_link_options(D3DRenderPipe PRIVATE
        /FORCE:MULTIPLE
        # Debug-specific linker flags
        $<$<CONFIG:Debug>:
        /DEBUG:FULL        # Full debug info
        /INCREMENTAL:NO    # Disable incremental linking in debug
        >
        # Release-specific linker flags
        $<$<CONFIG:Release>:
        /DEBUG             # Keep debug info even in Release (for crash dumps)
        >
)

target_include_directories(D3DRenderPipe PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(D3DRenderPipe PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)