cmake_minimum_required(VERSION 3.10)

project(mjsonrpc)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)

add_subdirectory(example/batch_invoke)
add_subdirectory(example/custom_error_info)
add_subdirectory(example/del_funcs)
add_subdirectory(example/invoke_a_func)
add_subdirectory(example/rpc_with_params)
