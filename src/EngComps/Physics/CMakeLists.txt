cmake_minimum_required(VERSION 3.10)
project(Physics LANGUAGES CXX)

# Sources
set(SOURCES
        abm.cpp
        algebra.cpp
        collide.cpp
        instance.cpp
        internal.cpp
        kintree.cpp
        pcontact.cpp
        physics.cpp
        pythag.cpp
        rbody.cpp
        rest.cpp
        rk.cpp
        sect.cpp
        spatial.cpp
        svdcmp.cpp
)

# DLL target
add_library(Physics SHARED ${SOURCES})

# Include directories
target_include_directories(Physics PRIVATE
        .
        ../..
        ../../Include
        ../../../Include
)

# Common compile/link flags
target_compile_options(Physics PRIVATE /W3 /EHsc)
target_link_options(Physics PRIVATE /INCREMENTAL:NO)

# Per-configuration compile flags
target_compile_options(Physics PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi /MTd>
        $<$<CONFIG:Release>:/O2 /Zi /MT>
)

# Per-configuration link flags
target_link_options(Physics PRIVATE
        $<$<CONFIG:Debug>:/DEBUG>
)

# Link libraries
target_link_libraries(Physics PRIVATE
        MathLib
        DACOM
        $<$<CONFIG:Debug>:winmm>
)

# Preprocessor definitions
target_compile_definitions(Physics PRIVATE
        _WINDOWS
        WIN32
)
target_compile_definitions(Physics PRIVATE
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)
