# Specify output as a shared library
add_library(RK4 SHARED rk4.cpp)

# Include directories (assuming headers are in the same folder)
target_include_directories(RK4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(RK4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../Include
)

# Link external libraries
target_link_libraries(RK4 PRIVATE
    DACOM
)

