# Create shared library (DLL on Windows)
add_library(Channel SHARED
        channel.cpp
)

# Include directories
target_include_directories(Channel PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Preprocessor definitions - common and configuration-specific
target_compile_definitions(Channel PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=1;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(Channel PRIVATE
        MathLib
        DACOM
)

# Warning level (all configurations)
target_compile_options(Channel PRIVATE /W3)

# String pooling
target_compile_options(Channel PRIVATE /GF)

# Configuration-specific compiler options
target_compile_options(Channel PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi>
        $<$<CONFIG:Release>:/O2;/Ob1;/Gy>
)

# Runtime library
target_compile_options(Channel PRIVATE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MT>
)

# Linker options
target_link_options(Channel PRIVATE
        /SUBSYSTEM:WINDOWS
        /FORCE:MULTIPLE
        $<$<CONFIG:Debug>:/DEBUG;/MAPFILE:Channel.map>
        $<$<CONFIG:Release>:/DEBUG;/MAPFILE:Channel.map>

)
