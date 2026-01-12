## CRT must be consistent with DACOM
#if (MSVC)
#    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
#endif()

project(adb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force Win32 like the original project
if (MSVC)
    set(CMAKE_GENERATOR_PLATFORM Win32)
endif()

add_executable(adb WIN32
        ADBFileImport.cpp
        database.CPP
        dbexport.cpp
        DbImport.cpp
        dbstuff.cpp
        dbsymbols.cpp
        dbtreeview.cpp
        export_mdb.cpp
        MicrosoftDatabaseExport.cpp
        PathExport.cpp
        Printheap.cpp
        RawData.cpp
        Userdefaults.CPP
        Pch.cpp
        database.RC
)

add_dependencies(adb DosFile)

target_include_directories(adb PRIVATE
        .
        ../../../Include
        ../../../Shared/Dinclude
)

# Precompiled headers
target_precompile_headers(adb PRIVATE
        Pch.h
)

# Common definitions
target_compile_definitions(adb PRIVATE
        WIN32
        _WINDOWS
)

# Config-specific definitions
target_compile_definitions(adb PRIVATE
        _AFXDLL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)

# Compiler options
target_compile_options(adb PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Zi /Od>
        $<$<CONFIG:Release>:/O2>
)

# Link dependencies (will fail until provided)
target_link_libraries(adb PRIVATE
        DACOM
        winmm
        comdlg32
        shell32
        odbc32
        comctl32
)

set_target_properties(adb PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_custom_command(TARGET adb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DACOM>
        $<TARGET_FILE_DIR:adb>
        COMMENT "Copying DACOM.dll to executable directory"
)

add_custom_command(TARGET adb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DosFile>
        $<TARGET_FILE_DIR:adb>
        COMMENT "Copying DosFile.dll to executable directory"
)
