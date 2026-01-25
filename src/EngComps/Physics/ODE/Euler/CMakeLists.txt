# Specify output as a shared library
add_library(Euler SHARED euler.cpp)

# Include directories (assuming headers are in the same folder)
target_include_directories(Euler PRIVATE .)

target_include_directories(Euler PRIVATE
        ..
        ../..
        ../../../../../Include
)

# Link external libraries
target_link_libraries(Euler PRIVATE
    DACOM
)

