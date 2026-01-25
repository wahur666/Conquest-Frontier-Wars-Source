# Source files
set(SOURCES
        hashtable.cpp
        property.cpp
)

# Create shared library
add_library(Property SHARED ${SOURCES})

# Include directories
target_include_directories(Property PRIVATE
        .
        ../../Include
        ../../../Include
        ../..
)

# Preprocessor definitions
target_compile_definitions(Property PRIVATE
        _WINDOWS
        _USRDLL
        PROPERTY_EXPORTS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(Property PRIVATE
        MathLib
        DACOM
        odbc32
        odbccp32
)

# MSVC compiler options
target_compile_options(Property PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)
