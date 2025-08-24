project(mjsonrpc-example-notif-req)

add_executable(${PROJECT_NAME} notif_req.c)

target_link_libraries(${PROJECT_NAME} mjsonrpc)
