
add_executable(phold phold.c)
target_include_directories(phold PRIVATE ${ROOTSIM_CORE_INCLUDE_PATH} ${ROOTSIM_RNG_INCLUDE_PATH} ${MPI_C_INCLUDE_PATH})
target_compile_options(phold PRIVATE ${MPI_C_COMPILE_FLAGS})
target_link_libraries(phold ${ROOTSIM_CORE_LIBRARIES} ${ROOTSIM_RNG_LIBRARIES} ${MPI_C_LIBRARIES} m)
