# Source and header files
set(SOURCES
        cmesh.cpp
        collision.cpp
        dup.cpp
        Gilbert.cpp
        vbox.cpp
        vclip.cpp
        vmesh.cpp
)

set(HEADERS
        DebugPrint.h
        Llist.h
        vbox.h
        vcyl.h
        vmesh.h
        xprim.h
)

# Create shared library
add_library(Collision SHARED ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(Collision PRIVATE
        .
        ../../Include
        ../../../Include
)

# Preprocessor definitions
target_compile_definitions(Collision PRIVATE
        _WINDOWS
        _USRDLL
        COLLISION_EXPORTS
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(Collision PRIVATE
        MathLib
        winmm
        DACOM
)

# MSVC compiler options
target_compile_options(Collision PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)
