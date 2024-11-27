
add_executable(sensors application.c forwarding_engine.c link_estimator.c link_layer.c physical_layer.c routing_engine.c parameters.c)
target_include_directories(sensors PRIVATE ${ROOTSIM_CORE_INCLUDE_PATH} ${ROOTSIM_RNG_INCLUDE_PATH} ${MPI_C_INCLUDE_PATH})
target_compile_options(sensors PRIVATE ${MPI_C_COMPILE_FLAGS})
target_link_libraries(sensors ${ROOTSIM_CORE_LIBRARIES} ${ROOTSIM_RNG_LIBRARIES} ${MPI_C_LIBRARIES} m)
