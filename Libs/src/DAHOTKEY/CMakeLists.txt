# Create shared library (DLL on Windows)
add_library(DAHotKey SHARED
        EventSys.cpp
        HKEvent.cpp
        HkeyRec.cpp
        Hotkey.rc
)

# Include directories
target_include_directories(DAHotKey PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../Include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Preprocessor definitions - common and configuration-specific
target_compile_definitions(DAHotKey PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(DAHotKey PRIVATE
        DACOM
        winmm
)

# Warning level (all configurations)
target_compile_options(DAHotKey PRIVATE /W3)

# String pooling
target_compile_options(DAHotKey PRIVATE /GF)

# Configuration-specific compiler options
target_compile_options(DAHotKey PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi>
        $<$<CONFIG:Release>:/O2;/Ob1;/Gy>
)

# Runtime library
target_compile_options(DAHotKey PRIVATE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MT>
)

# Linker options
target_link_options(DAHotKey PRIVATE
        /SUBSYSTEM:WINDOWS
        /FORCE:MULTIPLE
        $<$<CONFIG:Debug>:/DEBUG;/MAPFILE:DAHotkey.map>
        $<$<CONFIG:Release>:/DEBUG;/MAPFILE:DAHotkey.map>
)
