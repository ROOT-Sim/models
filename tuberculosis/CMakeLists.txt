
add_executable(tbc tbc.c guy.c guy_init.c parameters.c)
target_include_directories(tbc PRIVATE ${ROOTSIM_CORE_INCLUDE_PATH} ${ROOTSIM_RNG_INCLUDE_PATH} ${MPI_C_INCLUDE_PATH})
target_compile_options(tbc PRIVATE ${MPI_C_COMPILE_FLAGS})
target_link_libraries(tbc ${ROOTSIM_CORE_LIBRARIES} ${ROOTSIM_RNG_LIBRARIES} ${MPI_C_LIBRARIES} m)
