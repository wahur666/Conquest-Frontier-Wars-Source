cmake_minimum_required(VERSION 3.10)
project(Globals)

# Source files
add_library(Globals SHARED
        ../Globals.cpp
        ../Globals.rc
)

# Include directories
target_include_directories(Globals PRIVATE
        ../Include
        ../../../DInclude
        ../../../Include
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)

target_link_directories(Globals PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)

# Preprocessor definitions
target_compile_definitions(Globals PRIVATE
        WIN32
        _WINDOWS
        BUILD_DLL
        BUILD_GLOBALS
        DA_ERROR_LEVEL=7
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:DEMO_Debug>:_DEBUG;_DEMO_>
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:DEMO_Final>:NDEBUG;FINAL_RELEASE;_DEMO_>
        $<$<CONFIG:Final>:NDEBUG;FINAL_RELEASE>
)

# Compiler options
if (MSVC)
    target_compile_options(Globals PRIVATE
            /W4
            $<$<CONFIG:Debug>:/Od /Z7 /MTd>
            $<$<CONFIG:DEMO_Debug>:/Od /MTd>
            $<$<CONFIG:Release>:/O2 /Oi /GL /MT>
            $<$<CONFIG:DEMO_Final>:/O2 /Oi /GL /MT>
            $<$<CONFIG:Final>:/O2 /Oi /GL /MT>
    )
endif()

# DLL output name / no lib prefix
set_target_properties(Globals PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
)

# Linker settings
if (MSVC)
    target_link_options(Globals PRIVATE
            /SUBSYSTEM:WINDOWS
            # Entry point: only set for configs where the .vcxproj had it explicitly.
            # Release and Debug left it blank (linker picks default DLL entry).
            $<$<CONFIG:DEMO_Debug>:/ENTRY:DllMain>
            $<$<CONFIG:DEMO_Final>:/ENTRY:DllMain>
            $<$<CONFIG:Final>:/ENTRY:DllMain>
    )
endif()

# ---------------------------------------------------------------------------
# Custom build step: preprocess Data.cpp -> data.i
#   Mirrors the per-file VCCustomBuildTool in the original .vcxproj.
#   The .i file is consumed at runtime by the game's data loader, not by the
#   C++ compiler, so it is listed as an extra output rather than a source.
# ---------------------------------------------------------------------------
set(DATA_I_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/../data2.i")

add_custom_command(
        OUTPUT  "${DATA_I_OUTPUT}"
        COMMAND cl /P /EP /nologo
        "/I${CMAKE_CURRENT_SOURCE_DIR}/../../../DInclude"
        "/I${CMAKE_CURRENT_SOURCE_DIR}/../Include"
        "/I${CMAKE_CURRENT_SOURCE_DIR}/../../../Include"
        /D _ADB
        "/Fi${DATA_I_OUTPUT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Data.cpp"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../Data.cpp"
        COMMENT "Preprocessing Data.cpp -> data2.i\ndata2.i will be written to: ${DATA_I_OUTPUT}"
        VERBATIM
)
#add_custom_command(
#        TARGET Globals PRE_BUILD
#        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/xmiff.py" -i ../../DInclude ../sfxdata.xmf ../sfxdata.dat
#        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
#        COMMENT "Compiling sfxdata.xmf -> sfxdata.dat"
#        DEPENDS ../sfxdata.xmf ../../DInclude/sfxid.h
#)
add_custom_command(
        TARGET Globals PRE_BUILD
        COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/../xmiff.py"
        -i "${CMAKE_CURRENT_SOURCE_DIR}/../../../DInclude"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sfxdata.xmf"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sfxdata.dat"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Compiling sfxdata.xmf -> sfxdata.dat"
        VERBATIM
)

add_custom_target(Globals_DataI DEPENDS "${DATA_I_OUTPUT}")
add_dependencies(Globals Globals_DataI)