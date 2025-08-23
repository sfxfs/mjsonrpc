# Set the project name
project (cJSON)

# Add a library with the above sources
add_library(${PROJECT_NAME} SHARED cJSON.c)

target_include_directories( ${PROJECT_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}
)
