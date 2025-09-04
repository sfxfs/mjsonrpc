#include "mjsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Define a simple JSON-RPC method
cJSON* hello_world(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Hello, World!");
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t, '0' means default
    mjrpc_handle_t* handle = mjrpc_create_handle(0);

    // Add a method
    mjrpc_add_method(handle, hello_world, "hello", NULL);

    // Construct a JSON-RPC request: {"jsonrpc":"2.0","method":"hello","id":1}
    char* json_request = mjrpc_request_str("hello", NULL, cJSON_CreateNumber(1));
    char* json_response = NULL;

    // Process the request
    int result;
    json_response = mjrpc_process_str(handle, json_request, &result);

    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);
    // Assert that the response contains "Hello, World!"
    assert(json_response != NULL);

    // show the response
    printf("Request: %s\n", json_request);
    printf("Response: %s\n", json_response);
    free(json_request);
    free(json_response);

    // Cleanup
    mjrpc_destroy_handle(handle);

    // End of this example
    return 0;
}
