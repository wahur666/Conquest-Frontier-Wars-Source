cmake_minimum_required(VERSION 3.31)

project(CFW_Rebuild LANGUAGES CXX)

# ---- Global toolchain policy ----
if (MSVC)
    set(CMAKE_MFC_FLAG 0)
    add_compile_options(
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
    )
endif()

# ---- Language ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Output layout ----
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- Common warnings (don’t go crazy yet) ----
if (MSVC)
    add_compile_options(/W3)
endif()

# ---- Optional grouping (IDE sanity) ----
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- Third-party / platform ----
if (WIN32)
    add_compile_definitions(
            WIN32
            _WINDOWS
            NOMINMAX
    )
endif()

# -----------------------------
# Tier 0 — Low-level / Core
# -----------------------------
add_subdirectory(src/x86Math)          # math primitives, static lib
add_subdirectory(src/x86Math/mathlib)  # SIMD/x86 intrinsics
add_subdirectory(src/DACOM)            # messaging / COM wrappers
add_subdirectory(src/System)           # OS utilities
add_subdirectory(src/DOSFile)          # File I/O

# -----------------------------
# Tier 1 — Engine Components
# -----------------------------
add_subdirectory(src/Anim)             # animation system
add_subdirectory(src/Channel)          # engine channels / events
add_subdirectory(src/EngComps/Physics)          # physics
add_subdirectory(src/EngComps/Collision)        # collision system
add_subdirectory(src/EngComps/EngineCameras)    # camera system
add_subdirectory(src/EngComps/EngineLights)     # lighting system
add_subdirectory(src/EngComps/Hardpoint)     # Hardpoints
add_subdirectory(src/EngComps/Property)     # Property
add_subdirectory(src/EngComps/RenderMgr)     # renderer manager
add_subdirectory(src/RenderPipeline)     # renderer manager
add_subdirectory(src/RenderBatcher)     # render batcher
add_subdirectory(src/Engine)           # core engine
add_subdirectory(src/MeshManager) # Mesh manager
add_subdirectory(src/Streamer) # Streamer
add_subdirectory(src/StringTable) # string table
add_subdirectory(src/VertexBufferManager) # GPU buffer management
#add_subdirectory(src/EngComps/Physics/ODE/Euler)      # ODE physics, unused
#add_subdirectory(src/EngComps/Physics/ODE/RK4)      #  ODE physics, unused
add_subdirectory(src/EngComps/Physics/ODE/Trap)      # ODE physics
add_subdirectory(src/VideoSystem)

# -----------------------------
# Tier 2 — Rendering / Shading
# -----------------------------
add_subdirectory(src/RenderPipeline/RPUL)       # render pipeline library
add_subdirectory(src/Shading/LightManager)     # lighting
#add_subdirectory(src/Shading/Materials)        # materials
add_subdirectory(src/Shading/TextureLibrary)   # textures
add_subdirectory(src/RendComp/PolyMesh)        # polygon mesh
#add_subdirectory(src/RendComp/NURBMesh)        # NURBS library, Not portable, missing files, not used
add_subdirectory(src/RendComp/Optics)          # optics library
#add_subdirectory(src/RendComp/TriMesh)         # missing, triangle mesh library
add_subdirectory(src/MaterialManager)
add_subdirectory(src/TextureManager)

# -----------------------------
# Tier 3 — Tools / Viewers / Editors
# -----------------------------
add_subdirectory(src/Viewer/Docuview)          # viewer/debugger
add_subdirectory(src/Tools/ObjView)            # model viewer
add_subdirectory(src/Tools/adb)               # internal tools
add_subdirectory(src/DAHOTKEY)                 # missing, core hotkey library
add_subdirectory(src/ParticleEffect)    # particle effects

add_executable(CFW_Rebuild
        main.cpp
)

target_include_directories(CFW_Rebuild PRIVATE
        Include
)

target_link_libraries(CFW_Rebuild PRIVATE
        DACOM
        kernel32
)

set_target_properties(CFW_Rebuild PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_custom_command(TARGET CFW_Rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DACOM>
        $<TARGET_FILE_DIR:CFW_Rebuild>
        COMMENT "Copying DACOM.dll to executable directory"
)

add_custom_command(TARGET CFW_Rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DosFile>
        $<TARGET_FILE_DIR:CFW_Rebuild>
        COMMENT "Copying DosFile.dll to executable directory"
)
