add_library(MaterialManager SHARED
        Material.cpp
        MaterialManager.cpp
        MaterialWindow.cpp
        Modifier.cpp
        TextureList.cpp
        stdafx.cpp
        MaterialManager.rc
)


# Include paths
target_include_directories(MaterialManager PRIVATE
        .
        ../../Include
)

# Preprocessor defines
target_compile_definitions(MaterialManager PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        MATERIALMANAGER_EXPORTS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)


# Enable RTTI
target_compile_options(MaterialManager PRIVATE /GR)

# Warning level
target_compile_options(MaterialManager PRIVATE /W3)

# Disable 64-bit portability warnings (VC9 equivalent)
target_compile_definitions(MaterialManager PRIVATE _CRT_SECURE_NO_WARNINGS)

# Precompiled header (stdafx.h / stdafx.cpp)
target_precompile_headers(MaterialManager PRIVATE stdafx.h)

# Link libraries
target_link_libraries(MaterialManager PRIVATE
        DACOM
        comctl32
)


# Subsystem: Windows (/SUBSYSTEM:WINDOWS)
target_link_options(MaterialManager PRIVATE /SUBSYSTEM:WINDOWS)

target_include_directories(MaterialManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(MaterialManager PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)