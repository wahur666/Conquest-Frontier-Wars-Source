# Source files
set(SOURCES
        RPUL_Enumerate.cpp
        RPUL_Misc.cpp
        Text.cpp
)

# Header files (optional, for IDE)
set(HEADERS
        # Add headers here if you have them, e.g. RPUL.h
        ../../../Include/RPUL.h
)

# Include directories
include_directories(
        .
        ../../../Include
)

# Static library
add_library(RPUL STATIC ${SOURCES} ${HEADERS})

# Common definitions
target_compile_definitions(RPUL PRIVATE
        _WINDOWS
        WIN32
)

# Compiler definitions per configuration
target_compile_definitions(RPUL PRIVATE
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

target_link_libraries(RPUL PRIVATE d3d9 d3dx9)


target_include_directories(RPUL PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(RPUL PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)


## Optional: Output directories per configuration
#set_target_properties(RPUL PROPERTIES
#        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/../../../../Static/Debug
#        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/../../../../Static/Release
#)
#
## Post-build copy for release (optional, replicating your .vcxproj)
#add_custom_command(TARGET RPUL POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:RPUL> ${CMAKE_SOURCE_DIR}/../../../../Static/Release
#        COMMENT "Copy RPUL.lib after build"
#)
