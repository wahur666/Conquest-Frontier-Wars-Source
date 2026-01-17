# Specify output as a shared library
add_library(Euler SHARED euler.cpp)

# Include directories (assuming headers are in the same folder)
target_include_directories(Euler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(Euler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../Include
)

# Link external libraries
target_link_libraries(Euler PRIVATE
    DACOM
)

