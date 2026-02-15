cmake_minimum_required(VERSION 3.10)
project(Mission)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../DInclude
)

# Header files
set(MISSION_HEADERS
        ../common.h
        ../CommPacket.h
        ../../../DInclude/DMenuObjectives.h
        ../Include/Globals.h
        ../IBanker.h
        ../ISPlayerAI.h
        ../MapGen.h
        ../Include/MGlobals.h
        ../Mission.h
        ../Include/MPartRef.h
        ../Include/MScript.h
        ../NetVector.h
        ../ObjSet.h
        ../OpAgent.h
        ../RuseKeys.h
        ../RuseMap.h
        ../ScrollingText.h
        ../SPlayerAI.h
        ../UnitComm.h
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
        ../ObjMapIterator.cpp
        ../OpAgent.cpp
        ../Pch.cpp
        ../PlayerMenu.cpp
        ../ruse.cpp
        ../RuseMap.cpp
        ../ScrollingText.cpp
        ../Search.cpp
        ../SuperTrans.cpp
        ../UnitComm.cpp
)

# Data definition files
set(DATA_DEFINITIONS
        ../../../DInclude/DCQGame.h
        ../../../DInclude/DEndGame.h
        ../../../DInclude/DGlobalData.h
        ../../../DInclude/DMapGen.h
        ../../../DInclude/DPlayerMenu.h
)

# Create a library target (won't actually build, but enables IDE features)
add_library(Mission SHARED ${MISSION_SOURCES} ${MISSION_HEADERS} ${DATA_DEFINITIONS})

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
)

# Organize source files into groups for IDE
source_group("Header Files" FILES ${MISSION_HEADERS})
source_group("Source Files\\Strategic AI" FILES ${STRATEGIC_AI_SOURCES})
source_group("Source Files\\Menus" FILES ${MENU_SOURCES})
source_group("Source Files\\Game Menus" FILES ${GAME_MENU_SOURCES})
source_group("Source Files\\Core" FILES
        ../Banker.cpp
        ../Macrohelp.cpp
        ../MapGen.cpp
        ../MGlobals.cpp
        ../Mission.cpp
        ../MScript.cpp
        ../ObjMapIterator.cpp
        ../OpAgent.cpp
        ../Pch.cpp
        ../PlayerMenu.cpp
        ../ruse.cpp
        ../RuseMap.cpp
        ../ScrollingText.cpp
        ../SuperTrans.cpp
        ../UnitComm.cpp
)
source_group("Source Files\\Assembly" FILES ../Search.cpp)
source_group("Data Definitions" FILES ${DATA_DEFINITIONS})


target_include_directories(Mission PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(Mission PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)