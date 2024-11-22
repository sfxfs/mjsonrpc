#include <stdio.h>
#include <stdlib.h>
#include "mjsonrpc.h"

// Define a simple JSON-RPC method
cJSON *hello_world(mjrpc_ctx_t *context, cJSON *params, cJSON *id)
{
    cJSON *result = cJSON_CreateString("Hello, World!");
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t
    mjrpc_handler_t handle = {0};

    // Add a method
    mjrpc_add_method(&handle, hello_world, "hello", NULL);

    // Construct a JSON-RPC request
    const char *json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}";
    char *json_response = NULL;

    // Process the request
    int result;
    json_response = mjrpc_process_str(&handle, json_request, &result);

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

    return 0;
}
