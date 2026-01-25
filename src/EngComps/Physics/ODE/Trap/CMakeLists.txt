add_library(trap SHARED trap.cpp)

target_include_directories(trap PRIVATE
        .
        ../..
        ../../../../../Include
)

target_link_libraries(trap PRIVATE
        DACOM
        odbc32
        odbccp32
)

target_compile_definitions(trap PRIVATE _WINDOWS WIN32)

# Common compile and link flags
target_compile_options(trap PRIVATE /W3 /EHsc)
target_link_options(trap PRIVATE /INCREMENTAL:NO)

# Configuration-specific flags
target_compile_options(trap PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi /MTd /Gm>
        $<$<CONFIG:Release>:/O2 /Zi /MT>
)

target_link_options(trap PRIVATE
        $<$<CONFIG:Debug>:/DEBUG>
)
