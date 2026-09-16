cmake_minimum_required(VERSION 3.14)
project(nosql LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(MPI)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/FindROOTSim.cmake)

add_executable(nosql nosql.c ../common/argparse.c)
target_include_directories(nosql PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${ROOTSIM_CORE_INCLUDE_PATH}
    ${ROOTSIM_RNG_INCLUDE_PATH}
)
target_link_libraries(nosql
    ${ROOTSIM_CORE_LIBRARIES}
    ${ROOTSIM_RNG_LIBRARIES}
    m
    pthread
)

if(MPI_C_FOUND)
    target_include_directories(nosql PRIVATE ${MPI_C_INCLUDE_PATH})
    target_compile_options(nosql PRIVATE ${MPI_C_COMPILE_FLAGS})
    target_link_libraries(nosql ${MPI_C_LIBRARIES})
endif()
