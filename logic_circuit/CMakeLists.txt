cmake_minimum_required(VERSION 3.14)
project(logic_circuit LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(MPI)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/FindROOTSim.cmake)

add_executable(logic_circuit
    circuit.c
    bench_parser.c
    ../common/argparse.c
)

target_include_directories(logic_circuit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${ROOTSIM_CORE_INCLUDE_PATH}
    ${ROOTSIM_RNG_INCLUDE_PATH}
    ${ROOTSIM_TOPOLOGY_INCLUDE_PATH}
)

target_link_libraries(logic_circuit
    ${ROOTSIM_CORE_LIBRARIES}
    ${ROOTSIM_RNG_LIBRARIES}
    ${ROOTSIM_TOPOLOGY_LIBRARIES}
    m
    pthread
)

if(MPI_C_FOUND)
    target_include_directories(logic_circuit PRIVATE ${MPI_C_INCLUDE_PATH})
    target_compile_options(logic_circuit PRIVATE ${MPI_C_COMPILE_FLAGS})
    target_link_libraries(logic_circuit ${MPI_C_LIBRARIES})
endif()
