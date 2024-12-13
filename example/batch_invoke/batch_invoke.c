#include <stdio.h>
#include <stdlib.h>
#include "mjsonrpc.h"

// Define a simple JSON-RPC method
cJSON *hello_world(mjrpc_ctx_t *context, cJSON *params, cJSON *id)
{
    cJSON *result = cJSON_CreateString("Hello, World!");
    return result;
}

// Define another simple JSON-RPC method
cJSON *goodbye_world(mjrpc_ctx_t *context, cJSON *params, cJSON *id)
{
    cJSON *result = cJSON_CreateString("Goodbye, World!");
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t
    mjrpc_handle_t handle = {0};

    // Add multiple methods
    mjrpc_add_method(&handle, hello_world, "hello", NULL);
    mjrpc_add_method(&handle, goodbye_world, "goodbye", NULL);

    // Construct a batch JSON-RPC request
    const char *json_request = "[{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1},{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":2}]";

    // Process the request
    int result;
    char *json_response = mjrpc_process_str(&handle, json_request, &result);

    if (result != MJRPC_RET_OK)
    {
        printf("Error processing request: %d\n", result);
    }

    if (json_response)
    {
        printf("Response: %s\n", json_response);
        free(json_response);
    }

    // Cleanup
    mjrpc_del_method(&handle, "hello");
    mjrpc_del_method(&handle, "goodbye");

    return 0;
}
