# Specify output as a shared library
add_library(RK4 SHARED rk4.cpp)

# Include directories (assuming headers are in the same folder)
target_include_directories(RK4 PRIVATE .)

target_include_directories(RK4 PRIVATE
        ..
        ../..
        ../../../../../Include
)

# Link external libraries
target_link_libraries(RK4 PRIVATE
    DACOM
)

