cmake_minimum_required(VERSION 3.10)

project(mjsonrpc-example-del-funcs)
add_executable(${PROJECT_NAME} del_funcs.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PRIVATE
                            ../../src)
