# Set the project name
project (unity)

# Add a library with the above sources
add_library(${PROJECT_NAME} unity.c)

target_include_directories( ${PROJECT_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}
)
