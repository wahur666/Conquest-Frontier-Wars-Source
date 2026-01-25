# Source and headers
add_library(Streamer SHARED
        Streamer.cpp
)

# Include and library directories
target_include_directories(Streamer PRIVATE
        ../../Include
        ..
)

# Libraries
target_link_libraries(Streamer PRIVATE
        DACOM
        msacm32.lib
        $<$<CONFIG:Debug>:dxguid.lib>
)

# Common defines
target_compile_definitions(Streamer PRIVATE
        WIN32
        _WINDOWS
        DIRECTSOUND_VERSION=0x0600
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=2;DA_ERROR_LEVEL=3>
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
)

# Compiler options
target_compile_options(Streamer PRIVATE
        /W3
        $<$<CONFIG:Release>:/O2 /Oi /Ob1 /Gy /GF /Zi>
        $<$<CONFIG:Debug>:/Od /Zi>
)

# MSVC runtime and linker
set_target_properties(Streamer PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
