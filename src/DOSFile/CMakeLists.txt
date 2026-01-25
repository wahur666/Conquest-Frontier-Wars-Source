# Must be set BEFORE add_library
#if (MSVC)
#    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
#endif()

add_library(DosFile SHARED
        BaseUTF.cpp
        Dosfile.cpp
        MemFile.cpp
        SearchPath.cpp
        UTF.cpp
        UTFDMan.cpp
        UTFShare.cpp
        BaseUTF.h
        DosFileVer.h
        UTFDMan.h
        ../../Include/DACOM.H
        ../../Include/FDump.h
        ../../Include/Filesys.h
        ../../Include/HeapObj.h
        ../../Include/IUTFWriter.h
        ../../Include/MemFile.h
        ../../Include/Results.h
        ../../Include/SearchPath.h
        ../../Include/TComponent.h
        ../../Include/TSmartPointer.h
        ../../Include/TYPEDEFS.H
        ../libver.h
)

target_include_directories(DosFile
        PRIVATE
        .
        ../../Include
)

# Common definitions
target_compile_definitions(DosFile PRIVATE
        _WINDOWS
        WIN32
)

# Config-specific definitions
target_compile_definitions(DosFile PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Debug>:DA_ERROR_LEVEL=8>

        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:Release>:DA_ERROR_LEVEL=3>
)

# Compiler options
target_compile_options(DosFile PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Zi /Od>
        $<$<CONFIG:Release>:/Ox>
)

# Link libraries
target_link_libraries(DosFile PRIVATE
        DACOM
)
