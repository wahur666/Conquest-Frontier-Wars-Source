cmake_minimum_required(VERSION 3.31)
#
## MSVC runtime MUST be set before any targets or subdirectories
#if (MSVC)
#    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
#endif()

if (MSVC)
    set(CMAKE_MFC_FLAG 2)
endif()

project(CFW_Rebuild LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(src/DACOM)
add_custom_target(force_DACOM_rebuild ALL
        COMMAND ${CMAKE_COMMAND} -E echo "Forcing DACOM rebuild"
)
add_dependencies(DACOM force_DACOM_rebuild)
add_subdirectory(src/DOSFile)
add_subdirectory(src/Viewer/Docuview)
add_subdirectory(src/Tools/adb)

add_executable(CFW_Rebuild
        main.cpp
)

target_include_directories(CFW_Rebuild PRIVATE
        Include
)

target_link_libraries(CFW_Rebuild PRIVATE
        DACOM
        kernel32
)

set_target_properties(CFW_Rebuild PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_custom_command(TARGET CFW_Rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DACOM>
        $<TARGET_FILE_DIR:CFW_Rebuild>
        COMMENT "Copying DACOM.dll to executable directory"
)

add_custom_command(TARGET CFW_Rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DosFile>
        $<TARGET_FILE_DIR:CFW_Rebuild>
        COMMENT "Copying DosFile.dll to executable directory"
)
