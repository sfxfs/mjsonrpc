project(mjsonrpc-example-arg-to-function)

add_executable(${PROJECT_NAME} arg_to_function.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc)
