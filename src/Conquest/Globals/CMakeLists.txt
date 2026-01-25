cmake_minimum_required(VERSION 3.10)
project(Globals)

# Configuration types - use underscores instead of spaces for CMake sanity
set(CMAKE_CONFIGURATION_TYPES "DEMO_Debug;Release;DEMO_Final;Debug;Final" CACHE STRING "Configs" FORCE)
set(CMAKE_CONFIGURATION_TYPES_INIT "Debug")

# Platform
set(PLATFORM "Win32")

# Find Python3 for xmiff.py
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Shared include directories (all configs, relative to parent)
set(SHARED_INCLUDE_DIRS
        "../Include"
        "../../../DInclude"
        "../../../Include"
)

# Source files (in parent directory)
set(SOURCES
        "../Globals.cpp"
        "../Data.cpp"
)

# Header files (in parent directory)
set(HEADERS
        "../Include/Globals.h"
        "../DBHotkeys.h"
        "../Hotkeys.h"
        "../RuseKeys.h"
        "../resource.h"
)

# Resource files (in parent directory)
set(RESOURCE_FILES
        "../Globals.rc"
)

# Create DLL target
add_library(Globals SHARED ${SOURCES} ${HEADERS} ${RESOURCE_FILES})

# Include directories
target_include_directories(Globals PRIVATE ${SHARED_INCLUDE_DIRS})

# Preprocessor definitions per configuration
target_compile_definitions(Globals PRIVATE
        $<$<CONFIG:Debug>:BUILD_DLL;BUILD_GLOBALS;DA_ERROR_LEVEL=7;WIN32;_DEBUG;_WINDOWS>
        $<$<CONFIG:Release>:BUILD_GLOBALS;BUILD_DLL;DA_ERROR_LEVEL=7;WIN32;NDEBUG;_WINDOWS>
        $<$<CONFIG:DEMO_Debug>:BUILD_DLL;BUILD_GLOBALS;DA_ERROR_LEVEL=7;WIN32;_DEBUG;_WINDOWS;_DEMO_>
        $<$<CONFIG:DEMO_Final>:BUILD_GLOBALS;BUILD_DLL;DA_ERROR_LEVEL=7;WIN32;NDEBUG;_WINDOWS;FINAL_RELEASE;_DEMO_>
        $<$<CONFIG:Final>:BUILD_GLOBALS;BUILD_DLL;DA_ERROR_LEVEL=7;WIN32;NDEBUG;_WINDOWS;FINAL_RELEASE>
)

# MSVC-specific settings
if(MSVC)
    # Optimization flags per configuration
    target_compile_options(Globals PRIVATE
            $<$<CONFIG:Debug>:/Od>
            $<$<CONFIG:DEMO_Debug>:/Od>
            $<$<CONFIG:Release>:/O2 /Oi /GL>
            $<$<CONFIG:DEMO_Final>:/O2 /Oi /GL>
            $<$<CONFIG:Final>:/O2 /Oi /GL>
    )

    # Warning level (all configs)
    target_compile_options(Globals PRIVATE /W4)

    # Runtime library selection
    target_compile_options(Globals PRIVATE
            $<$<CONFIG:Debug>:/MTd>
            $<$<CONFIG:DEMO_Debug>:/MT>
            $<$<CONFIG:Release>:/MT>
            $<$<CONFIG:DEMO_Final>:/MT>
            $<$<CONFIG:Final>:/MT>
    )

    # Debug info format for Debug config
    target_compile_options(Globals PRIVATE
            $<$<CONFIG:Debug>:/Z7>
    )

    # DLL-specific settings
    set_target_properties(Globals PROPERTIES
            PREFIX ""
            SUFFIX ".dll"
    )

    # Linker flags
    target_link_options(Globals PRIVATE
            /SUBSYSTEM:WINDOWS
            /HEAP:100
            /RANDOMBASE
            /NXCOMPAT:NO
    )

    # Entry point for specific configs
    target_link_options(Globals PRIVATE
            $<$<CONFIG:Debug>:/ENTRY:>
            $<$<CONFIG:DEMO_Debug>:/ENTRY:DllMain>
            $<$<CONFIG:Release>:/ENTRY:>
            $<$<CONFIG:DEMO_Final>:/ENTRY:DllMain>
            $<$<CONFIG:Final>:/ENTRY:DllMain>
    )
endif()

# Custom build step: Preprocess Data.cpp
add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/data.i"
        COMMAND cl /P /EP /nologo /I../../DInclude /D_ADB ../Data.cpp
        WORKING_DIRECTORY "."
        DEPENDS ../Data.cpp
        COMMENT "Preprocessing Data.cpp"
)

## Custom build step: Compile sfxdata.xmf to sfxdata.dat using xmiff.py
#add_custom_command(
#        TARGET Globals PRE_BUILD
#        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/xmiff.py" -i ../../DInclude ../sfxdata.xmf ../sfxdata.dat
#        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
#        COMMENT "Compiling sfxdata.xmf -> sfxdata.dat"
#        DEPENDS ../sfxdata.xmf ../../DInclude/sfxid.h
#)
#
## Custom build step: Resource compilation (pre-link)
#if(MSVC)
#    add_custom_command(
#            TARGET Globals PRE_LINK
#            COMMAND rc /l 0x409 /fo Globals.res ../Globals.rc
#            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
#            COMMENT "Compiling resources"
#    )
#endif()

target_include_directories(Globals PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(Globals PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)