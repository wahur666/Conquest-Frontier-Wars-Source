# Source files
add_library(ParticleEffect SHARED
        ParticleEffect.cpp
        ParticleParamFunctions.cpp
        Particles.cpp
        stdafx.cpp
        ../../Include/IParticleManager.h
        ../../Include/ParticleParameters.h
        ParticleParamFunctions.h
        ParticleSaveStructs.h
        ParticleSystem.h
        stdafx.h
        PFAcceleration.h
        PFBillboardRender.h
        PFBoxShaper.h
        PFCameraShake.h
        PFCameraSort.h
        PFCylinderShaper.h
        PFDirectionalGravity.h
        PFDistanceDeltaHitter.h
        PFEventEmmiter.h
        PFEventKiller.h
        PFFixedEmmiter.h
        PFInitialDirectedVelocity.h
        PFInitialVelocity.h
        PFMatModColor.h
        PFMatModFloat.h
        PFMeshCut.h
        PFMeshInst.h
        PFMeshRef.h
        PFMeshRender.h
        PFMeshSlice.h
        PFMotionInterp.h
        PFPointEmmiter.h
        PFPointGravity.h
        PFShutdownTimer.h
        PFSpawn.h
        PFSphereHitter.h
        PFSphereShaper.h
        PFTargetShaper.h
)

# Include directories
target_include_directories(ParticleEffect PRIVATE
        ../../Include
)

# Libraries
target_link_libraries(ParticleEffect PRIVATE
        DACOM
        MathLib
)

# Common defines
target_compile_definitions(ParticleEffect PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)

# Compiler options
target_compile_options(ParticleEffect PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /Oi /Ob1 /Gy /Zi>
)

# Linker settings
set_target_properties(ParticleEffect PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")

target_include_directories(ParticleEffect PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(ParticleEffect PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)