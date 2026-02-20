# Set the project name
project (mjsonrpc)

# Add a library with the above sources
add_library(${PROJECT_NAME} SHARED mjsonrpc.c)

target_compile_definitions(${PROJECT_NAME} PRIVATE _DEFAULT_SOURCE)

target_include_directories( ${PROJECT_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC cJSON)
