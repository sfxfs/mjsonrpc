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

    // Construct a JSON-RPC request
    const char *json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":1}";
    int result;

    // Process the request
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

    // Delete a method
    mjrpc_del_method(&handle, "goodbye");

    // Attempt to call the deleted method
    json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":1}";
    json_response = mjrpc_process_str(&handle, json_request, &result);

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

    return 0;
}
