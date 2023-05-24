cmake_minimum_required(VERSION 3.12)
project("ROOT-Sim models" LANGUAGES C DESCRIPTION "Various C models for ROOT-Sim")

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(MPI REQUIRED)

# This is a manual configuration of the variables, because we don't have a proper Config.cmake file
# provided by the libraries.
# Currently working with the develop branch of the core and the master branch of the rng library.
set(ROOTSIM_CORE_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/include")
set(ROOTSIM_CORE_LIBRARIES "${CMAKE_SOURCE_DIR}/lib/librscore.a")
set(ROOTSIM_RNG_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/include")
set(ROOTSIM_RNG_LIBRARIES "${CMAKE_SOURCE_DIR}/lib/librsrng.a")
set(ROOTSIM_TOPOLOGY_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/include")
set(ROOTSIM_TOPOLOGY_LIBRARIES "${CMAKE_SOURCE_DIR}/lib/librstopology.a")

add_subdirectory(phold)
add_subdirectory(traffic)
