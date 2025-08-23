project(mjsonrpc-example-error-cases)

add_executable(${PROJECT_NAME} error_cases.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc cJSON)
