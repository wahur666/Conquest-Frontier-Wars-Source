# Source files
add_library(VideoSystem SHARED
        DllMain.cpp
        VideoSystem.cpp
        ../../include/VideoSystem.h
)

# Include directories
target_include_directories(VideoSystem PRIVATE
       ../../Include
)

#target_link_directories(VideoSystem PRIVATE
#        ../../static
#)

# Libraries
target_link_libraries(VideoSystem PRIVATE
        DACOM
        odbc32
        odbccp32
#        binkw32 # this is missing, since it is propitiatory
        $<$<CONFIG:Release>:Strmiids>
)

# Common defines
target_compile_definitions(VideoSystem PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        VIDEOSYSTEM_EXPORTS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=1>
)

# Compiler options
target_compile_options(VideoSystem PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /Oi /Ob1 /Gy /Zi -QaxW>
)

# Linker settings
set_target_properties(VideoSystem PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")

target_include_directories(VideoSystem PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(VideoSystem PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)