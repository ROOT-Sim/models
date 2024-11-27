
add_executable(nosql nosql.c)
target_include_directories(nosql PRIVATE ${ROOTSIM_CORE_INCLUDE_PATH} ${ROOTSIM_RNG_INCLUDE_PATH} ${MPI_C_INCLUDE_PATH})
target_compile_options(nosql PRIVATE ${MPI_C_COMPILE_FLAGS})
target_link_libraries(nosql ${ROOTSIM_CORE_LIBRARIES} ${ROOTSIM_RNG_LIBRARIES} ${MPI_C_LIBRARIES} m)
