# Source files
set(SOURCES
        engine.cpp
)

# Create shared library
add_library(Engine SHARED ${SOURCES})

# Include directories
target_include_directories(Engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Preprocessor definitions
target_compile_definitions(Engine PRIVATE
        _WINDOWS
        WIN32
        LAZY_UPDATE=1
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8;BUILD_LEVEL="Debug">
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3;BUILD_LEVEL="Release">
)

# Link libraries
target_link_libraries(Engine PRIVATE
        MathLib
        DACOM
)


# MSVC compiler options
target_compile_options(Engine PRIVATE
        /W3
        /Zi
        $<$<CONFIG:Release>:/O2;/Oi>
)

# Linker options
target_link_options(Engine PRIVATE
        $<$<CONFIG:Debug>:/FORCE:MULTIPLE>
)