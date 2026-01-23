# Source files
add_library(VertexBufferManager SHARED
        CopyVertexBufferDesc.cpp
        VertexBufferManager.cpp
)

# Include directories
target_include_directories(VertexBufferManager PRIVATE
        .
        ..
        ../..
        ../../include
)

# Libraries - common
target_link_libraries(VertexBufferManager PRIVATE
        winmm
        MathLib
        DACOM
        ddraw
        RPUL
)

# Common defines
target_compile_definitions(VertexBufferManager PRIVATE
        WIN32
        _WINDOWS
        D3D_OVERLOADS
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=3>
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
)

# Compiler options
target_compile_options(VertexBufferManager PRIVATE
        /GF
        $<$<CONFIG:Release>:/O2 /Oi /Ob1 /Gy /W4 /Zi>
        $<$<CONFIG:Debug>:/Od /W3 /Zi>
)


# Linker settings
set_target_properties(VertexBufferManager PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")


target_include_directories(VertexBufferManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(VertexBufferManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)