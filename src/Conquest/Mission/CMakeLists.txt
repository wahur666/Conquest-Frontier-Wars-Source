project(Mission)

# Include directories
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../DInclude
)

# Strategic AI source files
set(STRATEGIC_AI_SOURCES
        ../MCorvetteRush.cpp
        ../MDeny.cpp
        ../MDreadnoughts.cpp
        ../MForgers.cpp
        ../MFortress.cpp
        ../MForwardBuild.cpp
        ../MFrigateRush.cpp
        ../MStandardMantisAI.cpp
        ../MStandardSolarianAI.cpp
        ../MSwarm.cpp
        ../SPlayerAI.cpp
)

# Menu source files
set(MENU_SOURCES
        ../CMenu_BuildInd.cpp
        ../CMenu_Fabricator.cpp
        ../CMenu_FindButtons.cpp
        ../CMenu_Fleet.cpp
        ../CMenu_GenPlat.cpp
        ../CMenu_Ind.cpp
        ../CMenu_MulMix.cpp
        ../CMenu_None.cpp
        ../CMenu_ResBuild.cpp
        ../CMenu_Research.cpp
        ../CMenu_WarPlat.cpp
)

# Game menu source files
set(GAME_MENU_SOURCES
        ../Menu_AdmiralBar.cpp
        ../Menu_Chat.cpp
        ../Menu_Diplomacy.cpp
        ../Menu_EndGame.cpp
        ../Menu_netunloading.cpp
        ../Menu_Objectives.cpp
        ../Menu_PlayerChat.cpp
)

# Core source files
set(MISSION_SOURCES
        ${STRATEGIC_AI_SOURCES}
        ${MENU_SOURCES}
        ${GAME_MENU_SOURCES}
        ../Banker.cpp
        ../Macrohelp.cpp
        ../MapGen.cpp
        ../MGlobals.cpp
        ../Mission.cpp
        ../MScript.cpp
#        ../ObjList.cpp
        ../ObjMapIterator.cpp
        ../OpAgent.cpp
        ../Pch.cpp
        ../PlayerMenu.cpp
        ../ruse.cpp
        ../RuseMap.cpp
        ../ScrollingText.cpp
        ../Search.hpp
        ../SuperTrans.cpp
        ../UnitComm.cpp
)


# Create a library target (won't actually build, but enables IDE features)
add_library(Mission SHARED ${MISSION_SOURCES} )

# Add include directories to target
target_include_directories(Mission PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../DInclude
)

# Add compile definitions from the original project
target_compile_definitions(Mission PRIVATE
        BUILD_MISSION
        DA_ERROR_LEVEL=7
        WIN32
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:Final>:NDEBUG;FINAL_RELEASE>
        $<$<CONFIG:DemoDebug>:_DEBUG;_DEMO_>
        $<$<CONFIG:DemoFinal>:NDEBUG;FINAL_RELEASE;_DEMO_>

        $<$<CONFIG:Debug>:/Od /RTCs /Zi /W4>
        $<$<CONFIG:Release>:/O2 /Ob1 /Oi /Ot /W4 /Zi>
        $<$<CONFIG:Final>:/O2 /Ob1 /Oi /Ot /W4>
        $<$<CONFIG:DemoDebug>:/Od /Zi /W4>
        $<$<CONFIG:DemoFinal>:/O2 /Ob1 /Oi /Ot /W4>
)

target_include_directories(Mission PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(Mission PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)

target_link_libraries(Mission PRIVATE
        DXGuid
        MathLib
        DACOM
        RPUL
        Globals
        Trim
)

set_property(TARGET RPUL PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
