project(mjsonrpc_tests)

add_executable(basic_test basic_test.c)
target_link_libraries(basic_test PRIVATE unity mjsonrpc)

add_executable(extra_test extra_test.c)
target_link_libraries(extra_test PRIVATE unity mjsonrpc)

add_test(NAME basic_test COMMAND basic_test)
add_test(NAME extra_test COMMAND extra_test)
