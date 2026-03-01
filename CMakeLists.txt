cmake_minimum_required(VERSION 3.10)

project(mjsonrpc
    VERSION 2.4.0
    DESCRIPTION "A lightweight JSON-RPC 2.0 message parser and generator based on cJSON"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)

# Include GNUInstallDirs for standard installation directories
include(GNUInstallDirs)

add_subdirectory(src)
add_subdirectory(3rd)
add_subdirectory(example)

enable_testing()
add_subdirectory(test)

# Add uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()
