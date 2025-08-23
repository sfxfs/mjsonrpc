project(mjsonrpc-example-error-cases)
add_executable(${PROJECT_NAME} error_cases.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PRIVATE
                            ../../src)
