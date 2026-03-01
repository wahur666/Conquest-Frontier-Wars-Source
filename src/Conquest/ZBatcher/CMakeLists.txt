cmake_minimum_required(VERSION 3.10)
project(ZBatcher)

# Source files
add_library(ZBatcher SHARED
        ../BMPoolJr.cpp
        ../RenderBatcher.cpp
        ../BMPoolJr.h
        ../CQBatch.h
        ../RenderBatcher.h
)

# Include directories
target_include_directories(ZBatcher PRIVATE
        ../include
        ../../../DInclude
        ../../../Include
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)

target_link_directories(ZBatcher PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)

# Preprocessor definitions
target_compile_definitions(ZBatcher PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        ZBATCHER_EXPORTS
        DA_ERROR_LEVEL=7
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:DEMO_Debug>:_DEBUG;_DEMO_>
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:DEMO_Final>:NDEBUG;FINAL_RELEASE;_DEMO_>
        $<$<CONFIG:Final>:NDEBUG;FINAL_RELEASE>
)

# Compiler options
if (MSVC)
    target_compile_options(ZBatcher PRIVATE
            /W3
            $<$<CONFIG:Debug>:/Od /Zi /MTd /RTC1>
            $<$<CONFIG:DEMO_Debug>:/Od /Zi /MT /RTC1>
            $<$<CONFIG:Release>:/O2 /Oi /MT>
            $<$<CONFIG:DEMO_Final>:/O2 /Oi /MT>
            $<$<CONFIG:Final>:/O2 /Oi /MT>
    )
endif()

# DLL output name / no lib prefix
set_target_properties(ZBatcher PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
)

# Linker settings
if (MSVC)
    target_link_options(ZBatcher PRIVATE
            /FORCE:MULTIPLE
            /SUBSYSTEM:WINDOWS
            $<$<CONFIG:DEMO_Debug>:/ENTRY:DllMain>
            $<$<CONFIG:DEMO_Final>:/ENTRY:DllMain>
    )
endif()

# Dependencies
target_link_libraries(ZBatcher PRIVATE
        Globals
        DACOM
        MathLib
        Trim
)