if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(ParticleEditor CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(SOURCES
    "../Common/EditField.cpp"
    "../Common/GetColorSelection.cpp"
    "../Common/GetProfileInfo.cpp"
    "../Common/GetTextureSelection.cpp"
    "Camera.cpp"
    "ParticleEditor.cpp"
    "ParticleEditor.rc"
)

add_executable(ParticleEditor ${SOURCES})
# Include directories
target_include_directories(ParticleEditor PRIVATE
    ../Common
    ../../../Include
)
target_include_directories(ParticleEditor PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
)
target_link_directories(ParticleEditor PRIVATE
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
)
target_link_libraries(ParticleEditor PRIVATE
    RPUL
    Comctl32
    DACOM
    MathLib
    winmm
)

# Preprocessor definitions
target_compile_definitions(ParticleEditor PRIVATE
    _WINDOWS
    WIN32
    $<IF:$<CONFIG:Debug>,_DEBUG;DA_ERROR_LEVEL=8,NDEBUG;DA_ERROR_LEVEL=3>
)

set_target_properties(ParticleEditor PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "Applications"
        WIN32_EXECUTABLE TRUE
)
