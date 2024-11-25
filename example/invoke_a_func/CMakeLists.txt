project(mjsonrpc-example-invoke-a-func)
add_executable(${PROJECT_NAME} invoke_a_func.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PRIVATE
                            ../../src)
