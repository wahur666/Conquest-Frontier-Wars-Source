cmake_minimum_required(VERSION 3.10)
project(ZBatcher)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../DInclude
)

# Source files
set(ZBATCHER_SOURCES
        ../BMPoolJr.cpp
        ../RenderBatcher.cpp
)

# Header files
set(ZBATCHER_HEADERS
        ../BMPoolJr.h
        ../CQBatch.h
        ../RenderBatcher.h
)

# Create a library target (won't actually build, but enables IDE features)
add_library(ZBatcher SHARED ${ZBATCHER_SOURCES} ${ZBATCHER_HEADERS})

# Add include directories to target
target_include_directories(ZBatcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../DInclude
)

# Optional: Add compile definitions from the original project
target_compile_definitions(ZBatcher PRIVATE
        _MBCS
        _USRDLL
        ZBATCHER_EXPORTS
        WIN32
)

target_include_directories(ZBatcher PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(ZBatcher PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)