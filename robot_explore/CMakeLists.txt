
add_executable(robot_explore robot_explore.c topology_utils.c)
target_include_directories(robot_explore PRIVATE ${ROOTSIM_CORE_INCLUDE_PATH} ${ROOTSIM_RNG_INCLUDE_PATH} ${MPI_C_INCLUDE_PATH})
target_compile_options(robot_explore PRIVATE ${MPI_C_COMPILE_FLAGS})
target_link_libraries(robot_explore ${ROOTSIM_CORE_LIBRARIES} ${ROOTSIM_RNG_LIBRARIES} ${MPI_C_LIBRARIES} m)
