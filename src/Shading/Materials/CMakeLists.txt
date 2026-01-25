# Source files
set(SOURCES
        ../../Tools/Common/Shapes.cpp
        BaseMaterial.cpp
        BumpMaterial.cpp
        DcDtBtEcMaterial.cpp
        DcDtBtMaterial.cpp
        DcDtEcEtMaterial.cpp
        DcDtEtMaterial.cpp
        DebugMaterial.cpp
        EcEtMaterial.cpp
        EmbossBumpMaterial.cpp
        MaterialLibrary.cpp
        Materials.cpp
        NullMaterial.cpp
        perlin.cpp
        ProceduralBase.cpp
        ProceduralManager.cpp
        ProceduralMaterial.cpp
        ProceduralNoise.cpp
        ProceduralSwirl.cpp
        ReflectionMaterial.cpp
        SimpleMaterial.cpp
        SpecularMaterial.cpp
        StateMaterial.cpp
        ToonShadeMaterial.cpp
        TwoSidedMaterial.cpp
)

# Header files
set(HEADERS
        BaseMaterial.h
        Materials.h
        perlin.h
        ProceduralNoise.h
        ProceduralSwirl.h
)

# Include directories
include_directories(
        .
        ..
        ../..
        ../../Include
        ../../../Include
)

# Create DLL/shared library
add_library(Materials SHARED ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(Materials
        winmm
        MathLib
        DACOM
        ddraw.lib
        RPUL
)

# Preprocessor definitions
target_compile_definitions(Materials PRIVATE
        $<$<CONFIG:Debug>:_DEBUG DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG DA_ERROR_LEVEL=3>
        _WINDOWS
        WIN32
        D3D_OVERLOADS
)


# Warning level
target_compile_options(Materials PRIVATE /W4)

# Optimization
target_compile_options(Materials PRIVATE
        $<$<CONFIG:Release>:/O2 /Oi /Gy>
        $<$<CONFIG:Debug>:/Od>
)

# String pooling
target_compile_options(Materials PRIVATE /GF)


target_include_directories(Materials PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(Materials PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)