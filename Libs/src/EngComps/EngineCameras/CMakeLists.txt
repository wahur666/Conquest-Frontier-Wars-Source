# Create shared library
add_library(EngineCameras SHARED EngineCameras.cpp)

# Include directories
target_include_directories(EngineCameras PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
)

# Preprocessor definitions
target_compile_definitions(EngineCameras PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(EngineCameras PRIVATE
        DACOM
        MathLib
)

# MSVC compiler options
target_compile_options(EngineCameras PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2>
)
