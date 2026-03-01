# Set the project name
project (mjsonrpc)

# Version information
set(MJSONRPC_VERSION_MAJOR 2)
set(MJSONRPC_VERSION_MINOR 4)
set(MJSONRPC_VERSION_PATCH 0)
set(MJSONRPC_VERSION ${MJSONRPC_VERSION_MAJOR}.${MJSONRPC_VERSION_MINOR}.${MJSONRPC_VERSION_PATCH})

# Add a shared library
add_library(${PROJECT_NAME} SHARED mjsonrpc.c)

# Add static library option
option(BUILD_STATIC_LIBRARY "Build static library" OFF)
if(BUILD_STATIC_LIBRARY)
    add_library(${PROJECT_NAME}_static STATIC mjsonrpc.c)
    target_compile_definitions(${PROJECT_NAME}_static PRIVATE _DEFAULT_SOURCE)
    set_target_properties(${PROJECT_NAME}_static PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
        VERSION ${MJSONRPC_VERSION}
        SOVERSION ${MJSONRPC_VERSION_MAJOR}
    )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE _DEFAULT_SOURCE)

target_include_directories( ${PROJECT_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC cJSON)

# Set library properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${MJSONRPC_VERSION}
    SOVERSION ${MJSONRPC_VERSION_MAJOR}
    PUBLIC_HEADER mjsonrpc.h
)

# Install targets
include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_STATIC_LIBRARY)
    install(TARGETS ${PROJECT_NAME}_static
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

# Install header
install(FILES mjsonrpc.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Configure pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/mjsonrpc.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/mjsonrpc.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mjsonrpc.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
