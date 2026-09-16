# Helper to find ROOT-Sim libraries and headers
# Looks in CMake cache variables, environment, or relative paths to core/rng/topology repos

if(NOT ROOTSIM_CORE_INCLUDE_PATH)
    find_path(ROOTSIM_CORE_INCLUDE_PATH NAMES ROOT-Sim.h
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../core/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../core/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ENV ROOTSIM_CORE_INCLUDE_PATH
    )
endif()

if(NOT ROOTSIM_CORE_LIBRARIES)
    find_library(ROOTSIM_CORE_LIBRARIES NAMES rscore
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../core/build/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../core/build/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ENV ROOTSIM_CORE_LIBRARIES
    )
endif()

if(NOT ROOTSIM_RNG_INCLUDE_PATH)
    find_path(ROOTSIM_RNG_INCLUDE_PATH NAMES ROOT-Sim/random.h
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../random-number-generators/src/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../random-number-generators/src/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ENV ROOTSIM_RNG_INCLUDE_PATH
    )
endif()

if(NOT ROOTSIM_RNG_LIBRARIES)
    find_library(ROOTSIM_RNG_LIBRARIES NAMES rsrng
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../random-number-generators/build/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../random-number-generators/build/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ENV ROOTSIM_RNG_LIBRARIES
    )
endif()

if(NOT ROOTSIM_TOPOLOGY_INCLUDE_PATH)
    find_path(ROOTSIM_TOPOLOGY_INCLUDE_PATH NAMES ROOT-Sim/topology.h
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../topology/src/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../topology/src/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ENV ROOTSIM_TOPOLOGY_INCLUDE_PATH
    )
endif()

if(NOT ROOTSIM_TOPOLOGY_LIBRARIES)
    find_library(ROOTSIM_TOPOLOGY_LIBRARIES NAMES rstopology
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../topology/build/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../topology/build/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ENV ROOTSIM_TOPOLOGY_LIBRARIES
    )
endif()
