
set(SOURCES dc_bus_shift_test.c)
add_executable(bus_shift_test ${SOURCES})
target_link_libraries(bus_shift_test soem rt)
install(TARGETS bus_shift_test DESTINATION bin)
