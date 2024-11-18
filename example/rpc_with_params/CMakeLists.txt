cmake_minimum_required(VERSION 3.10)

project(mjsonrpc-example-rpc-with-params)
add_executable(${PROJECT_NAME} rpc_with_params.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PRIVATE
                            ../../src)
