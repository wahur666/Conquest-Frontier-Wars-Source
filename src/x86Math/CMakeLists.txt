# Create shared library (DLL on Windows)
add_library(x86Math SHARED
        x86math.cpp
)

# Include directories
target_include_directories(x86Math PRIVATE
        .
        ../../Include
)

# Preprocessor definitions - common and configuration-specific
target_compile_definitions(x86Math PRIVATE
        _WINDOWS
        BUILD_MATHLIB
        WIN32
        $<$<CONFIG:Debug>:_DEBUG;DA_ERROR_LEVEL=8>
        $<$<CONFIG:Release>:NDEBUG;DA_ERROR_LEVEL=1;DA_ERROR_LEVEL=3>
)

# Link libraries - configuration-specific
target_link_libraries(x86Math PRIVATE
        DACOM
)

# Warning level (all configurations)
target_compile_options(x86Math PRIVATE /W3)

# String pooling and function-level linking (all configurations)
target_compile_options(x86Math PRIVATE /GF /Gy)

# Configuration-specific compiler options
target_compile_options(x86Math PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi;/Zf>
        $<$<CONFIG:Release>:/O2;/GL;/Ob1>
)

# Linker options
target_link_options(x86Math PRIVATE
        /SUBSYSTEM:WINDOWS
        /FORCE:MULTIPLE
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Release>:/LTCG;/MAP>
)

