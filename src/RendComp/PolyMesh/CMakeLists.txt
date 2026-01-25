# Source files
set(SOURCES
        Mesh.cpp
        polymesh.cpp
        sortq.cpp
)

# Create shared library
add_library(PolyMesh SHARED ${SOURCES})

# Include directories
target_include_directories(PolyMesh PRIVATE
        .
        ../../Include
        ../../../Include
        ../..
)

# Preprocessor definitions
target_compile_definitions(PolyMesh PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(PolyMesh PRIVATE
        MathLib
        DACOM
        winmm
        $<$<CONFIG:Release>:ddraw>
)

# MSVC compiler options
target_compile_options(PolyMesh PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)

# Linker options
target_link_options(PolyMesh PRIVATE
        /FORCE:MULTIPLE
)


target_include_directories(PolyMesh PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(PolyMesh PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)