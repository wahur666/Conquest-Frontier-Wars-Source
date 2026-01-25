
# Source files
set(SOURCES
        MeshArch.cpp
        MeshInstance.cpp
        MeshManager.cpp
        stdafx.cpp
)

# Create DLL
add_library(MeshManager SHARED ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(MeshManager PRIVATE
        ../../Include
)

# Link libraries
target_link_libraries(MeshManager PRIVATE
        DACOM
        MathLib
        d3dx9
)

# Preprocessor definitions
target_compile_definitions(MeshManager PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)

# Compiler options
target_compile_options(MeshManager PRIVATE
        /W3
        /WX-
)

# Linker flags
set_target_properties(MeshManager PROPERTIES
        LINK_FLAGS "/FORCE:MULTIPLE"
)

# Enable function-level linking for Release
target_compile_options(MeshManager PRIVATE
        $<$<CONFIG:Release>:/Gy>
)

# Optimization settings
target_compile_options(MeshManager PRIVATE
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Release>:/O2 /Oi>
)

target_include_directories(MeshManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(MeshManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)