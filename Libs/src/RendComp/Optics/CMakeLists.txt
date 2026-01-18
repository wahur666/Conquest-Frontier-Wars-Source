# Source files
set(SOURCES
        optics.cpp
        ParticleSystemArchetype.cpp
        ParticleSystemInstance.cpp
)

# Create shared library
add_library(Optics SHARED ${SOURCES})

# Include directories
target_include_directories(Optics PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../Include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../
)

# Preprocessor definitions
target_compile_definitions(Optics PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(Optics PRIVATE
        MathLib
        DACOM
)

# MSVC compiler options
target_compile_options(Optics PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)

# Linker options
target_link_options(Optics PRIVATE
        /FORCE:MULTIPLE
)

target_include_directories(Optics PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(Optics PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)