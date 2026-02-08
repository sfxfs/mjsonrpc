project(mjsonrpc_tests)

add_executable(client_test client_test.c)
target_link_libraries(client_test PRIVATE unity mjsonrpc)

add_executable(arry_test arry_test.c)
target_link_libraries(arry_test PRIVATE unity mjsonrpc)

add_executable(err_test err_test.c)
target_link_libraries(err_test PRIVATE unity mjsonrpc)

add_executable(obj_test obj_test.c)
target_link_libraries(obj_test PRIVATE unity mjsonrpc)

add_executable(mem_test mem_test.c)
target_link_libraries(mem_test PRIVATE unity mjsonrpc)

add_executable(regression_test regression_test.c)
target_link_libraries(regression_test PRIVATE unity mjsonrpc)

add_test(NAME client_test COMMAND client_test)
add_test(NAME arry_test COMMAND arry_test)
add_test(NAME err_test COMMAND err_test)
add_test(NAME obj_test COMMAND obj_test)
add_test(NAME mem_test COMMAND mem_test)
add_test(NAME regression_test COMMAND regression_test)
