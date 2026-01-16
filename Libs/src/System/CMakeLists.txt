# Create shared library (DLL on Windows)
add_library(System SHARED
        SysContainer.cpp
        WindowManager.cpp
)

# Include directories
target_include_directories(System PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Include
)

# Preprocessor definitions - common and configuration-specific
target_compile_definitions(System PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=1;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(System PRIVATE
        DACOM
)

# Warning level (all configurations)
target_compile_options(System PRIVATE /W3)

# String pooling
target_compile_options(System PRIVATE /GF)

# Configuration-specific compiler options
target_compile_options(System PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi>
        $<$<CONFIG:Release>:/Ob1;/Gy>
)

# Runtime library
target_compile_options(System PRIVATE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MT>
)

# Linker options
target_link_options(System PRIVATE
        /SUBSYSTEM:WINDOWS
        /FORCE:MULTIPLE
        $<$<CONFIG:Debug>:/DEBUG;/MAPFILE:System.map>
        $<$<CONFIG:Release>:/DEBUG;/MAPFILE:System.map>
)