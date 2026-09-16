cmake_minimum_required(VERSION 3.14)
project(sensors LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(MPI)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/FindROOTSim.cmake)

add_executable(sensors
    application.c
    forwarding_engine.c
    link_estimator.c
    link_layer.c
    physical_layer.c
    routing_engine.c
    parameters.c
    ../common/argparse.c
)
target_include_directories(sensors PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${ROOTSIM_CORE_INCLUDE_PATH}
    ${ROOTSIM_RNG_INCLUDE_PATH}
)
target_link_libraries(sensors
    ${ROOTSIM_CORE_LIBRARIES}
    ${ROOTSIM_RNG_LIBRARIES}
    m
    pthread
)

if(MPI_C_FOUND)
    target_include_directories(sensors PRIVATE ${MPI_C_INCLUDE_PATH})
    target_compile_options(sensors PRIVATE ${MPI_C_COMPILE_FLAGS})
    target_link_libraries(sensors ${MPI_C_LIBRARIES})
endif()
