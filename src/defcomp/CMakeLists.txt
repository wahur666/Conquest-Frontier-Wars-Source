# Create shared library (DLL on Windows)
add_library(defcomp SHARED
        defcomp.cpp
        defdyn.cpp
        defmesh.cpp
        deform.cpp
        deformIK.cpp
        def_patch.cpp
        eulerangles.cpp
        StdAfx.cpp
)

# Include directories
target_include_directories(defcomp PRIVATE
        ../../Include
        ../Include
        ..
)

# Common preprocessor definitions
target_compile_definitions(defcomp PRIVATE
        _WINDOWS
        _USRDLL
        DEFCOMP_EXPORTS
        WIN32
)

# Configuration-specific preprocessor definitions
target_compile_definitions(defcomp PRIVATE
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(defcomp PRIVATE
        MathLib
        DACOM
        odbc32
        odbccp32
)

# Warning level (all configurations)
target_compile_options(defcomp PRIVATE /W3)

# String pooling
target_compile_options(defcomp PRIVATE /GF)

# Configuration-specific compiler options
target_compile_options(defcomp PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi;/RTCu>
        $<$<CONFIG:Release>:/O2;/Ob1;/Gy>
)

# Linker options
target_link_options(defcomp PRIVATE
        /FORCE:MULTIPLE
        $<$<CONFIG:Debug>:/DEBUG;/MAPFILE:deformable.map>
        $<$<CONFIG:Release>:/DEBUG;/MAPFILE:deformable.map>
)

target_include_directories(defcomp PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(defcomp PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)