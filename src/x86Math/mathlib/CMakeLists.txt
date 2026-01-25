# Create static library
add_library(MathLib STATIC
        MathLib.cpp
)

# Include directories
target_include_directories(MathLib PRIVATE
        ../../../Include
)

# Preprocessor definitions - common and configuration-specific
target_compile_definitions(MathLib PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=1;DA_ERROR_LEVEL=3>
)


# Warning level
target_compile_options(MathLib PRIVATE /W3)

# String pooling
target_compile_options(MathLib PRIVATE /GF)

# Configuration-specific compiler options
target_compile_options(MathLib PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi>
        $<$<CONFIG:Release>:/O2;/Ob1;/Gy>
)

# Runtime library
target_compile_options(MathLib PRIVATE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MD>
)

## Set library output location
#set_target_properties(MathLib PROPERTIES
#        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../Static"
#)