# ============================================================================
# CONQUEST GAME EXECUTABLE
# ============================================================================
add_executable(Conquest
        Conquest.rc
        Aegis.cpp
        AirDefense.cpp
        anim2d.cpp
        AnimObj.cpp
        AnmBolt.cpp
        AntiMatter.cpp
        arc.cpp
        AreaEffectBolt.cpp
        ArtileryLauncher.cpp
        AutoCannon.cpp
        BarrageLauncher.cpp
        BaseObject.cpp
        beam.cpp
        blackhole.cpp
        blast.cpp
        Blinkers.cpp
        BuffLauncher.cpp
        BuildPlat.cpp
        BuildShip.cpp
        BuildSupPlat.cpp
        CloakEffect.cpp
        CloakLauncher.cpp
        cloud.cpp
        CommTrack.cpp
        Conquest.cpp
        CQExtent.cpp
        Damage.cpp
        debris.cpp
        Destabilizer.cpp
        DumbReconProbe.cpp
        Dust.cpp
        EngineTrail.cpp
        explosion.cpp
        Fabricator.cpp
        FancyLaunch.cpp
        Fighter.cpp
        FighterWing.cpp
        FireBall.cpp
        FlagShip.cpp
        FogOfWar.cpp
        GattlingBeam.cpp
        GeneralPlat.cpp
        GroupObj.cpp
        Gunboat.cpp
        GunPlat.cpp
        HarvestShip.cpp
        jumpgate.cpp
        JumpLauncher.cpp
        JumpPlat.cpp
        KamikazeWing.cpp
        light.cpp
        Macrohelp.cpp
        MantisBuild.cpp
        MassDisruptor.cpp
        MeshExplode.cpp
        MeshInfo.cpp
        MeshObj.cpp
        MeshRender.cpp
        Mimic.cpp
        MineField.cpp
        MineLayer.cpp
        MorphMesh.cpp
        MovieCamera.cpp
        MultiCloakLauncher.cpp
        Nugget.cpp
        ObjComm.cpp
        ObjControl.cpp
        ObjectGenerator.cpp
        ObjGen.cpp
        ObjList.cpp
        ObjMap.cpp
        ObjMapIterator.cpp
        ObjMove.cpp
        Overdrive.cpp
        ParticleObj.cpp
        Pch.cpp
        PingLauncher.cpp
        PlanetKillerBolt.cpp
        Planetoid.cpp
        PlasmaBolt.cpp
        PlayerBomb.cpp
        Projectile.cpp
        RandomGen.cpp
        ReconLaunch.cpp
        ReconProbe.cpp
        RefinePlat.cpp
        RepairPlat.cpp
        RepellentCloud.cpp
        Repulsor.cpp
        RepulsorWave.cpp
        ScriptObject.cpp
        Search.asm
        Sector.cpp
        SellPlat.cpp
        SimpleMesh.cpp
        SolarianBuild.cpp
        SpaceShip.cpp
        SpaceWave.cpp
        SpecialBolt.cpp
        SpiderDrone.cpp
        Splash.cpp
        StacticInit.cpp
        StatisBolt.cpp
        Streak.cpp
        SuperTrans.cpp
        SupplyShip.cpp
        Swapper.cpp
        Synthesis.cpp
        SysMap.cpp
        TalorianEffect.cpp
        TalorianLauncher.cpp
        TerrainMap.cpp
        TerranBuild.cpp
        TerranDrone.cpp
        Testbed.cpp
        Tractor.cpp
        TractorWaveLauncher.cpp
        Trail.cpp
        Trigger.cpp
        TroopPod.cpp
        Troopship.cpp
        Turret.cpp
        UIAnim.cpp
        UnbornMeshList.cpp
        VLaunch.cpp
        Waypoint.cpp
        WormholeBlast.cpp
        WormholeLauncher.cpp
        Zealot.cpp
)

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================
target_include_directories(Conquest PRIVATE
        Include
        ../../DInclude
        ../../Include
)

# ============================================================================
# TARGET-SPECIFIC COMPILER OPTIONS
# ============================================================================
if (MSVC)
    target_compile_options(Conquest PRIVATE
            /W4
            $<$<CONFIG:Debug>:/Zi /Od>
            $<$<CONFIG:Release>:/Zi /Ox /Ot /Oa /Ow /Og /Oi /Oy->
    )
endif()

# ============================================================================
# TARGET-SPECIFIC COMPILE DEFINITIONS
# ============================================================================
target_compile_definitions(Conquest PRIVATE
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=7>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=7;FINAL_RELEASE>
)

# Add _DEMO_ for demo builds if needed
# target_compile_definitions(Conquest PRIVATE $<$<CONFIG:DemoDebug>:_DEMO_>)

# ============================================================================
# LINK LIBRARIES
# ============================================================================
target_link_libraries(Conquest PRIVATE
        # Foundation (static)
        DACOM
        MathLib

        # Game modules (static)
        Globals
        Mission
        Trim
        ZBatcher

        # System/Windows libs
        kernel32.lib
        user32.lib
        gdi32.lib
        winspool.lib
        comdlg32.lib
        advapi32.lib
        shell32.lib
        ole32.lib
        oleaut32.lib
        uuid.lib
        odbc32.lib
        odbccp32.lib
        COMCTL32.lib
        DXGuid.lib
        d3dx.lib
)
# ============================================================================
# IDE PROPERTIES
# ============================================================================
set_target_properties(Conquest PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "Applications"
)


target_include_directories(Conquest PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(Conquest PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)