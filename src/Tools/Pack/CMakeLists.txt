if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(Pack CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    "pack.cpp"
)


add_executable(Pack ${SOURCES})

target_include_directories(Pack PRIVATE
    ../../../Include
)

target_compile_definitions(Pack PRIVATE
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
)

target_compile_options(Pack PRIVATE
        /W4
        $<$<CONFIG:Debug>:/Zi /Od>
        $<$<CONFIG:Release>:/O2 /Gy /GF>
)

target_link_libraries(Pack PRIVATE
    DACOM
)

set_target_properties(Pack PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

