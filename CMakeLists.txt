cmake_minimum_required(VERSION 3.10)

project(mjsonrpc)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)

add_subdirectory(src)
add_subdirectory(3rd)
add_subdirectory(example)

enable_testing()
add_subdirectory(test)
