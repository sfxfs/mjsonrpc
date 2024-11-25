project(mjsonrpc-example-custom-error-info)
add_executable(${PROJECT_NAME} custom_error_info.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PRIVATE
                            ../../src)
