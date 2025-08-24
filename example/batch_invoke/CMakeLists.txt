project(mjsonrpc-example-batch-invoke)

add_executable(${PROJECT_NAME} batch_invoke.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc)
