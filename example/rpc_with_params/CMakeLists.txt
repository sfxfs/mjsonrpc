project(mjsonrpc-example-rpc-with-params)

add_executable(${PROJECT_NAME} rpc_with_params.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc)
