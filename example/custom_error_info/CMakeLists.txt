project(mjsonrpc-example-custom-error-info)

add_executable(${PROJECT_NAME} custom_error_info.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc cJSON)
