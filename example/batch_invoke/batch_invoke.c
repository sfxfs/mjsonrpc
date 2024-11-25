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
    mjrpc_handler_t handle = {0};

    // Add multiple methods
    mjrpc_add_method(&handle, hello_world, "hello", NULL);
    mjrpc_add_method(&handle, goodbye_world, "goodbye", NULL);

    // Construct a batch JSON-RPC request
    const char *json_request = "[{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1},{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":2}]";
    char *json_response = NULL;

    // Process the request
    int result = mjrpc_process_str(&handle, json_request, &json_response);

    if (result == MJRPC_RET_OK)
    {
        printf("Response: %s\n", json_response);
        free(json_response);
    }
    else
    {
        printf("Error processing request: %d\n", result);
    }

    // Cleanup
    mjrpc_del_method(&handle, "hello");
    mjrpc_del_method(&handle, "goodbye");

    return 0;
}
