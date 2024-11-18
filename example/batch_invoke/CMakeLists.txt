cmake_minimum_required(VERSION 3.10)

project(mjsonrpc-example-batch-invoke)
add_executable(${PROJECT_NAME} batch_invoke.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PRIVATE
                            ../../src)
