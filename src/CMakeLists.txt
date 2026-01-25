cmake_minimum_required(VERSION 3.31)
project(ConquestFrontierWars LANGUAGES CXX)

# ============================================================================
# GLOBAL TOOLCHAIN POLICY
# ============================================================================
if (MSVC)
    set(CMAKE_MFC_FLAG 0)
    add_compile_options(
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
    )
endif()

# ============================================================================
# LANGUAGE STANDARD
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# OUTPUT DIRECTORIES
# ============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# COMPILER WARNINGS
# ============================================================================
if (MSVC)
    add_compile_options(/W3)
endif()

# ============================================================================
# IDE ORGANIZATION
# ============================================================================
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ============================================================================
# PLATFORM DEFINITIONS
# ============================================================================
if (WIN32)
    add_compile_definitions(
            WIN32
            _WINDOWS
            NOMINMAX
    )
endif()



