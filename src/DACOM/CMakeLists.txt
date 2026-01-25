# Must be set BEFORE add_library
#if (MSVC)
#    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
#endif()

add_library(DACOM SHARED
        BaseHeap.cpp
        Dacom.cpp
        HeapInst.cpp
        MSHeap.cpp
        ProfileParser.cpp
        BaseHeap.h
        resource.h
        ../../Include/DACOM.H
        ../../Include/FDump.h
        ../../Include/HeapObj.h
        ../../Include/IProfileParser.h
        ../../Include/Results.h
        ../../Include/STDDAT.H
        ../../Include/TComponent.h
        ../../Include/TYPEDEFS.H
)

target_include_directories(DACOM
        PRIVATE
        .
        ../../Include
)

# Common definitions (both configs)
target_compile_definitions(DACOM PRIVATE
        _WINDOWS
        BUILD_DACOM
        WIN32
)

# Config-specific definitions (this is the critical fix)
target_compile_definitions(DACOM PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Debug>:DA_ERROR_LEVEL=8>

        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:Release>:DA_ERROR_LEVEL=3>
)

# Compiler options
target_compile_options(DACOM PRIVATE
        /W4
        $<$<CONFIG:Debug>:/Zi /Od>
        $<$<CONFIG:Release>:/Ox>
)

target_link_libraries(DACOM PRIVATE
        version
)
