project(mjsonrpc-example-del-funcs)

add_executable(${PROJECT_NAME} del_funcs.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc cJSON)
