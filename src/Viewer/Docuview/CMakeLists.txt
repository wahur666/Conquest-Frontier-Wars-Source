add_library(DataViewer SHARED
        DataParser.cpp
        DataView.cpp
        Document.cpp
        FERROR.cpp
        FILESTRM.cpp
        LXTABLES.cpp
        NUMBER.cpp
        PARS1.cpp
        PRTTOKEN.cpp
        SCANNER.cpp
        StringSet.cpp
        SYMTABLE.cpp
        treeview.cpp
        ViewCnst.cpp

        DataView.h
        Ferror.h
        IStructEnumerator.h
        Lxtables.h
        Parserrs.h
        Prttoken.h
        Symtable.h
        Tokendef.h
        treeview.h

        ../../../Include/IStringSet.h
        ../../../Include/ViewCnst.h
        ../../../Include/Viewer.h

        DataView.rc
)

# Output name differs from project name
set_target_properties(DataViewer PROPERTIES
        OUTPUT_NAME DocuView
)

target_include_directories(DataViewer PRIVATE
        .
        ../../../Include
        ../../../Include
)

# Common defines
target_compile_definitions(DataViewer PRIVATE
        _WINDOWS
        WIN32
)

# Config-specific defines
target_compile_definitions(DataViewer PRIVATE
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=1>
)

# Compiler options
target_compile_options(DataViewer PRIVATE
        /W4
        $<$<CONFIG:Debug>:/Zi /Od>
        $<$<CONFIG:Release>:/O2>
)

# Linker options
target_link_options(DataViewer PRIVATE
        /FORCE:MULTIPLE
)

target_link_libraries(DataViewer PRIVATE
        DACOM
        comctl32
)

# Match VS behavior: no ASLR
set_target_properties(DataViewer PROPERTIES
        LINK_FLAGS "/DYNAMICBASE:NO"
)