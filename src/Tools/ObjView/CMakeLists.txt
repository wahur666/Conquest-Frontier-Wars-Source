## CRT must be consistent with DACOM
#if (MSVC)
#    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
#endif()

project(CmpndView CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CmpndView WIN32
        ../Common/GetProfileInfo.cpp
        ../Common/Shapes.cpp
        ../Common/States.cpp
        animation.cpp
        cmpndview.cpp
        err.cpp
        Extent.cpp
        hardpoint.cpp
        joint.cpp
        Lights.cpp
        winstuff.cpp
        cmpndview.h
        Lights.h
        resource.h
        ObjView.rc
)

set_target_properties(CmpndView PROPERTIES OUTPUT_NAME ObjView)

target_include_directories(CmpndView PRIVATE
        .
        ../Common
        ../../../Include
        ../../../Include/RPUL
    "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)

target_link_directories(CmpndView PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)

target_compile_definitions(CmpndView PRIVATE
    _WINDOWS
    WIN32
    NWO
    HARDPOINTS
    DRAWEXTENTS
    LIGHT
    ANIMATION
    EXPLOSIONS
    DRAW_LIGHT
    LEGO
)

target_compile_definitions(CmpndView PRIVATE
    $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
    $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
)

target_compile_options(CmpndView PRIVATE
    /W4
    $<$<CONFIG:Debug>:/Zi /Od>
    $<$<CONFIG:Release>:/O2 /Gy /GF>
)

target_link_libraries(CmpndView PRIVATE
    DACOM
    MathLib
    RPUL
)

set_target_properties(CmpndView PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_custom_command(TARGET CmpndView POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DACOM>
        $<TARGET_FILE_DIR:CmpndView>
        COMMENT "Copying DACOM.dll to executable directory"
)
add_dependencies(CmpndView System DosFile x86Math)

add_custom_command(TARGET CmpndView POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:System>
        $<TARGET_FILE_DIR:CmpndView>
        COMMENT "Copying System.dll to executable directory"
)

add_custom_command(TARGET CmpndView POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DosFile>
        $<TARGET_FILE_DIR:CmpndView>
        COMMENT "Copying DosFile.dll to executable directory"
)

add_custom_command(TARGET CmpndView POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:x86Math>
        $<TARGET_FILE_DIR:CmpndView>
        COMMENT "Copying x86Math.dll to executable directory"
)