cmake_minimum_required(VERSION 3.10)

project(mjsonrpc-test)
add_executable(mjsonrpc-test test.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PUBLIC
                            ../../src)
