cmake_minimum_required(VERSION 3.10)
project(Trim)

# Source files
add_library(Trim SHARED
        ../Pch.cpp
        ../Animate.cpp
        ../BmpRead.cpp
        ../BuildButton.cpp
        ../Button2.cpp
        ../Camera.cpp
        ../Combobox.cpp
        ../CQImage.cpp
        ../cqpipeline.cpp
        ../Cursor.cpp
        ../DiplomacyButton.cpp
        ../DrawAgent.cpp
        ../DrawAgent16.cpp
        ../Dropdown.cpp
        ../DumpView.cpp
        ../Edit2.cpp
        ../EulaWin.cpp
        ../GameProgress.cpp
        ../GenData.cpp
        ../GridVector.cpp
        ../Hintbox.cpp
        ../HKManager.cpp
        ../HotButton.cpp
        ../HotStatic.cpp
        ../Icon.cpp
        ../IniConfig.cpp
        ../InProgressAnim.cpp
        ../LFParser.cpp
        ../LineManager.cpp
        ../lines.cpp
        ../Listbox.cpp
        ../LoadFont.cpp
        ../LogFile.cpp
        ../Macrohelp.cpp
        ../menu.cpp
        ../Menu1.cpp
        ../Menu_Briefing.cpp
        ../Menu_campaign.cpp
        ../Menu_Confirm.cpp
        ../Menu_Credits.cpp
        ../Menu_final.cpp
        ../Menu_help.cpp
        ../Menu_igoptions.cpp
        ../Menu_LoadSave.cpp
        ../Menu_map.cpp
        ../Menu_MapSelect.cpp
        ../Menu_mission.cpp
        ../Menu_mshell.cpp
        ../Menu_netconn.cpp
        ../Menu_netloading.cpp
        ../Menu_netsess.cpp
        ../Menu_netsess2.cpp
        ../Menu_newplayer.cpp
        ../Menu_options.cpp
        ../Menu_Pause.cpp
        ../Menu_SlideShow.cpp
        ../Menu_slots.cpp
        ../Menu_SPGame.cpp
        ../Menu_SysKitSaveLoad.cpp
        ../Menu_Toolbar.cpp
        ../Menu_zone.cpp
        ../Modal.cpp
        ../MovieScreen.cpp
        ../MScroll.cpp
        ../MultiHotButton.cpp
        ../MultiLineFont.cpp
        ../MusicManager.cpp
        ../NetBuffer.cpp
        ../NetConnect.cpp
        ../NetConnectBuffers.cpp
        ../NetFileTransfer.cpp
        ../NetPacket.cpp
        ../Objwatch.cpp
        ../PosTool.cpp
        ../PrintHeap.cpp
        ../ProgressStatic.cpp
        ../QueueControl.cpp
        ../ResearchButton.cpp
        ../ScrollBar.cpp
        ../SFX.cpp
        ../ShapeLoader.cpp
        ../ShipSilButton.cpp
        ../Slider.cpp
        ../SoundManager.cpp
        ../SpaceEnv.cpp
        ../Static.cpp
        ../StatusBar.cpp
        ../Streamer.cpp
        ../StringData.cpp
        ../Subtitle.cpp
        ../SuperTrans.cpp
        ../SysContainer.cpp
        ../System.cpp
        ../TabButton.cpp
        ../TabControl.cpp
        ../Teletype.cpp
        ../TestScript.cpp
        ../Tgaread.cpp
        ../TManager.cpp
        ../Trim.cpp
        ../UserDefaults.cpp
        ../VertexBuffer.cpp
        ../VfxRead.cpp
        ../VideoSurface.cpp
        ../VoxCompress.cpp
        ../WindowManager.cpp
        # ASM sources - assembled via MASM
        ../Search.cpp

#        Winvfx16.asm
)

# DSStream.cpp and VidStream.cpp are excluded from Debug build only
# They are included in all other configs via a generator expression exclusion trick:
# CMake has no per-file per-config exclude, so we use COMPILE_OPTIONS to
# effectively no-op them, OR simply add them and note they may fail in Debug.
# The cleanest approach: add them unconditionally and let the developer
# exclude them manually if needed, matching original intent with a comment.
#
# To properly exclude only in Debug, use set_source_files_properties:
set_source_files_properties(DSStream.cpp VidStream.cpp PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:/WX->"  # soften errors in debug if needed
)
# NOTE: If these files cause link errors in Debug, exclude them:
# set_source_files_properties(DSStream.cpp VidStream.cpp PROPERTIES
#         HEADER_FILE_ONLY "$<CONFIG:Debug>")
# (HEADER_FILE_ONLY doesn't support genexes — exclude manually or use a workaround)
target_sources(Trim PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:../DSStream.cpp>
        $<$<NOT:$<CONFIG:Debug>>:../VidStream.cpp>
)

# Include directories
target_include_directories(Trim PRIVATE
        ../include
        ../../../DInclude
        ../../../Include
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)

target_link_directories(Trim PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)

# Preprocessor definitions
target_compile_definitions(Trim PRIVATE
        WIN32
        _WINDOWS
        BUILD_TRIM
        DA_ERROR_LEVEL=7
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:DEMO_Debug>:_DEBUG;_DEMO_>
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:DEMO_Final>:NDEBUG;FINAL_RELEASE;_DEMO_>
        $<$<CONFIG:Final>:NDEBUG;FINAL_RELEASE>
)

# Compiler options
if (MSVC)
    target_compile_options(Trim PRIVATE
            /W4
            $<$<CONFIG:Debug>:/Od /Zi /MTd>
            $<$<CONFIG:DEMO_Debug>:/Od /Zi /MT>
            $<$<CONFIG:Release>:/O2 /Oi /Ob1 /Gs /MT>
            $<$<CONFIG:DEMO_Final>:/O2 /Oi /Ob1 /Gs /MT>
            $<$<CONFIG:Final>:/O2 /Oi /Ob1 /Gs /MT>
    )
endif()

# DLL settings
set_target_properties(Trim PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
)

# Module definition file
set_target_properties(Trim PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/Trim.def\""
)

# Linker settings
if (MSVC)
    target_link_options(Trim PRIVATE
            /SUBSYSTEM:WINDOWS
            $<$<CONFIG:Debug>:/FORCE:MULTIPLE>
            $<$<CONFIG:DEMO_Final>:/FORCE:MULTIPLE>
    )
endif()

# System and DirectX libraries
target_link_libraries(Trim PRIVATE
        Globals
        MathLib
        DACOM
        # Prebuilt static libs from Libs/Static - adjust path as needed
        # COMHeap     # comheap.lib
        # rpul        # rpul.lib
        ddraw
        dinput
        dsetup
        shlwapi
        version
        imm32
        winmm
        msacm32
        DXGuid
        # Release/Final only:
        $<$<NOT:$<CONFIG:Debug>>:strmiids>
        $<$<NOT:$<CONFIG:Debug>>:amstrmid>
)