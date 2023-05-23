
add_executable(traffic application.c functions.c init.c normal_cdf.c)
target_include_directories(traffic PRIVATE ${ROOTSIM_CORE_INCLUDE_PATH} ${ROOTSIM_RNG_INCLUDE_PATH} ${MPI_C_INCLUDE_PATH})
target_compile_options(traffic PRIVATE ${MPI_C_COMPILE_FLAGS})
target_link_libraries(traffic ${ROOTSIM_CORE_LIBRARIES} ${ROOTSIM_RNG_LIBRARIES} ${MPI_C_LIBRARIES} m)
