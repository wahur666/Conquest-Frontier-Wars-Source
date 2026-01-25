add_executable(CFW_Rebuild
        main.cpp
)

target_include_directories(CFW_Rebuild PRIVATE
        ../../Include
)

target_link_libraries(CFW_Rebuild PRIVATE
        DACOM
        kernel32
)

set_target_properties(CFW_Rebuild PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)