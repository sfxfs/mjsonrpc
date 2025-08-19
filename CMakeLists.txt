cmake_minimum_required(VERSION 3.10)

project(mjsonrpc)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)

add_subdirectory(example/batch_invoke)
add_subdirectory(example/custom_error_info)
add_subdirectory(example/del_funcs)
add_subdirectory(example/invoke_a_func)
add_subdirectory(example/rpc_with_params)

# 添加CTest支持
enable_testing()

# 注册example下的可执行文件为ctest测试
add_test(NAME batch_invoke COMMAND mjsonrpc-example-batch-invoke)
add_test(NAME custom_error_info COMMAND mjsonrpc-example-custom-error-info)
add_test(NAME del_funcs COMMAND mjsonrpc-example-del-funcs)
add_test(NAME invoke_a_func COMMAND mjsonrpc-example-invoke-a-func)
add_test(NAME rpc_with_params COMMAND mjsonrpc-example-rpc-with-params)
