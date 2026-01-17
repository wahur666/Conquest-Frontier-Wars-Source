add_library(LightManager SHARED
        LightManager.cpp
)

# Includes
target_include_directories(LightManager PRIVATE
        .
        ..
        ../..
        ../../include
        ../../../Include
)

# Common defines
target_compile_definitions(LightManager PRIVATE
        _WINDOWS
        WIN32
        D3D_OVERLOADS
)

# Config-specific defines
target_compile_definitions(LightManager PRIVATE
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
)

# Runtime library (single, consistent model)
# Debug  -> /MTd
# Release -> /MT
#set_property(TARGET LightManager PROPERTY
#        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
#)

# Compiler options
target_compile_options(LightManager PRIVATE
        /GF
        $<$<CONFIG:Debug>:/Od;/Zi>
        $<$<CONFIG:Release>:/O2;/Ob1;/Gy>
)

# Linker options
target_link_options(LightManager PRIVATE
        /SUBSYSTEM:WINDOWS
)

# Libraries
target_link_libraries(LightManager PRIVATE
        winmm
        MathLib
        DACOM
        ddraw
        RPUL
        d3d9
        d3dx9
)


target_include_directories(LightManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(LightManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)