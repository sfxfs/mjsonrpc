project(mjsonrpc-example-mem-hooks)

add_executable(${PROJECT_NAME} memory_hooks.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc)
