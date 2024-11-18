#include <stdio.h>
#include <stdlib.h>
#include "mjsonrpc.h"

// Define a JSON-RPC method with parameters
cJSON *add(mjrpc_ctx_t *context, cJSON *params, cJSON *id)
{
    if (params == NULL || cJSON_GetArraySize(params) != 2)
    {
        context->error_code = JRPC_INVALID_PARAMS;
        context->error_message = strdup("Invalid params: Expected two numbers.");
        return NULL;
    }

    int a = cJSON_GetArrayItem(params, 0)->valueint;
    int b = cJSON_GetArrayItem(params, 1)->valueint;

    cJSON *result = cJSON_CreateNumber(a + b);
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t
    mjrpc_handle_t handle = {0};

    // Add a method
    mjrpc_add_method(&handle, add, "add", NULL);

    // Construct a JSON-RPC request with parameters
    const char *json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[2, 3],\"id\":1}";
    char *json_response = NULL;

    // Process the request
    int result = mjrpc_process(&handle, json_request, &json_response);

    if (result == MJRPC_OK)
    {
        printf("Response: %s\n", json_response);
        free(json_response);
    }
    else
    {
        printf("Error processing request: %d\n", result);
    }

    // Cleanup
    mjrpc_del_method(&handle, "add");

    return 0;
}
