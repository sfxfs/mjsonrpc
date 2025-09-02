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

// Define another simple JSON-RPC method
cJSON* goodbye_world(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Goodbye, World!");
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t, '0' means default
    mjrpc_handle_t* handle = mjrpc_create_handle(0);

    // Add multiple methods
    mjrpc_add_method(handle, hello_world, "hello", NULL);
    mjrpc_add_method(handle, goodbye_world, "goodbye", NULL);

    // Construct a batch JSON-RPC request
    const char* json_request = "["
                               "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1},"
                               "{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":2}"
                               "]";

    // Process the request
    int result;
    char* json_response = mjrpc_process_str(handle, json_request, &result);

    // Result check
    assert(result == MJRPC_RET_OK);
    assert(json_response != NULL);

    // Show the response
    printf("Response: %s\n", json_response);
    free(json_response);

    // Cleanup
    mjrpc_destroy_handle(handle);

    // End of this example
    return 0;
}
