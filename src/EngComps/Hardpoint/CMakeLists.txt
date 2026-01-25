# Source files
set(SOURCES
        HardPoint.cpp
)

# Create shared library
add_library(HardPoint SHARED ${SOURCES})

# Include directories
target_include_directories(HardPoint PRIVATE
        .
        ../../Include
        ../../../Include
        ../..
)

# Preprocessor definitions
target_compile_definitions(HardPoint PRIVATE
        _WINDOWS
        DA_HEAP_ENABLED
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(HardPoint PRIVATE
        MathLib
        DACOM
)

# Link directories
target_link_directories(HardPoint PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../Static/$<CONFIG>
)

# MSVC compiler options
target_compile_options(HardPoint PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)

# Linker options
target_link_options(HardPoint PRIVATE
        /FORCE:MULTIPLE
)

