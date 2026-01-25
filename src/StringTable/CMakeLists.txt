# Source and headers
add_library(StringTable SHARED
        StringTable.cpp
        ../XML/tinystr.cpp
        ../XML/tinyxml.cpp
        ../XML/tinyxmlerror.cpp
        ../XML/tinyxmlparser.cpp
        ../../Include/StringTable.h
        ../XML/tinystr.h
        ../XML/tinyxml.h
)

# Include and resource directories
target_include_directories(StringTable PRIVATE
        ../../Include
)

# Libraries
target_link_libraries(StringTable PRIVATE
        MathLib
        DACOM
)

# Common defines
target_compile_definitions(StringTable PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)

# Compiler options
target_compile_options(StringTable PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /Oi /Ob1 /Gy /GF /Zi>
)

# MSVC runtime and linker
set_target_properties(StringTable PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
