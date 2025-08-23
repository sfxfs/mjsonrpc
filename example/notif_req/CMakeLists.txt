project(mjsonrpc-example-notif-req)
add_executable(${PROJECT_NAME} notif_req.c ../../src/mjsonrpc.c ../../src/cJSON.c)

target_include_directories(${PROJECT_NAME} PRIVATE
                            ../../src)
