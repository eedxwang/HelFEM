# CMake project file for HelFEM
cmake_minimum_required (VERSION 2.6)
project (helfem CXX Fortran)

option(USE_OPENMP "Compile OpenMP enabled version (for parallel calculations)?" ON)
# Find OpenMP support
if(USE_OPENMP)
 find_package( OpenMP )
 if(OPENMP_FOUND)
  # Add flags to CXX flags
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
 endif()
endif()

# System specific definitions
include(CMake.system)

# Compile code
add_subdirectory(src)
