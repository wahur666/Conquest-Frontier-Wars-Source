add_library(Anim SHARED
        Anim.cpp
        AnimComponent.cpp
        channel.cpp
        mstack.cpp
)

# Include directories
target_include_directories(Anim PRIVATE
        ../../Include
)

# Preprocessor definitions - common and configuration-specific
target_compile_definitions(Anim PRIVATE
        _WINDOWS
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=1;DA_ERROR_LEVEL=3>
)

# Link libraries
target_link_libraries(Anim PRIVATE
        DACOM
        MathLib
)


# Warning level (all configurations)
target_compile_options(Anim PRIVATE /W3)

# String pooling
target_compile_options(Anim PRIVATE /GF)

# Configuration-specific compiler options
target_compile_options(Anim PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi>
        $<$<CONFIG:Release>:/O2;/Ob1;/Gy>
)

# Runtime library
target_compile_options(Anim PRIVATE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MT>
)

# Linker options
target_link_options(Anim PRIVATE
        /SUBSYSTEM:WINDOWS
        /FORCE:MULTIPLE
        $<$<CONFIG:Debug>:/DEBUG;/MAPFILE:Anim.map>
        $<$<CONFIG:Release>:/DEBUG;/MAPFILE:Anim.map>
)
