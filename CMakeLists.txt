cmake_minimum_required(VERSION 3.15)
project(Conquest)

# ============================================================================
# BUILD LIBS FIRST (Foundation + all DLLs)
# ============================================================================
add_subdirectory(Libs)
add_subdirectory(App)

