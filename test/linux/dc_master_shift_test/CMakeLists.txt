
set(SOURCES dc_master_shift_test.c)
add_executable(master_shift_test ${SOURCES})
target_link_libraries(master_shift_test soem rt)
install(TARGETS master_shift_test DESTINATION bin)
