add_executable(CFW_Rebuild
        main.cpp
)

target_include_directories(CFW_Rebuild PRIVATE
        ../../Include
)

target_link_libraries(CFW_Rebuild PRIVATE
        DACOM
        dbghelp
        kernel32
)

add_custom_target(CSharpDacomProbe
        COMMAND dotnet publish
                "${CMAKE_CURRENT_SOURCE_DIR}/CSharpDacomProbe/CSharpDacomProbe.csproj"
                -c $<CONFIG>
                -r win-x64
                --self-contained true
                -o "$<TARGET_FILE_DIR:CFW_Rebuild>"
        COMMENT "Publishing C# DACOM NativeAOT probe"
        VERBATIM
)

add_dependencies(CFW_Rebuild CSharpDacomProbe)

set_target_properties(CFW_Rebuild PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)
