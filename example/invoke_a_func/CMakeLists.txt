project(mjsonrpc-example-invoke-a-func)

add_executable(${PROJECT_NAME} invoke_a_func.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc cJSON)
