# Create shared library
add_library(EngineLights SHARED EngineLights.cpp)

# Include directories
target_include_directories(EngineLights PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../Include
)

# Preprocessor definitions
target_compile_definitions(EngineLights PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(EngineLights PRIVATE
        DACOM
        MathLib
)

# MSVC compiler options
target_compile_options(EngineLights PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)
