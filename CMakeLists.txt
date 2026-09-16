cmake_minimum_required(VERSION 3.14)
project("ROOT-Sim models" LANGUAGES C DESCRIPTION "Various C models for ROOT-Sim")

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(MPI)

include(${CMAKE_CURRENT_SOURCE_DIR}/common/FindROOTSim.cmake)

add_subdirectory(nosql)
add_subdirectory(pcs)
add_subdirectory(phold)
add_subdirectory(robot_explore)
add_subdirectory(segregation)
add_subdirectory(sensors)
add_subdirectory(stupid_model)
add_subdirectory(sugarscape)
add_subdirectory(tcar)
add_subdirectory(traffic)
add_subdirectory(tuberculosis)

