# Set the project name
project (mjsonrpc)

# Add a library with the above sources
add_library(${PROJECT_NAME} mjsonrpc.c)

target_include_directories( ${PROJECT_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} cJSON)
