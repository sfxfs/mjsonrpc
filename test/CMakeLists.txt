project(mjsonrpc_tests)

add_executable(basic_test basic_test.c)
target_link_libraries(basic_test PRIVATE unity mjsonrpc cJSON)

add_test(NAME basic_test COMMAND basic_test)
