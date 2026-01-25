# Source files
set(SOURCES
        chull.cpp
        RenderMgr.cpp
)

# Create shared library
add_library(RenderMgr SHARED ${SOURCES})

# Include directories
target_include_directories(RenderMgr PRIVATE
        .
        ../../Include
        ../../../Include
        ../..
)

# Preprocessor definitions
target_compile_definitions(RenderMgr PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(RenderMgr PRIVATE
        DACOM
        MathLib
        d3dx9d
        d3d9
)

# MSVC compiler options
target_compile_options(RenderMgr PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)

# Linker options
target_link_options(RenderMgr PRIVATE
        /FORCE:MULTIPLE
)

target_include_directories(RenderMgr PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(RenderMgr PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)