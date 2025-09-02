#include "mjsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Define a JSON-RPC method with parameters
cJSON* add(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    if (params == NULL || cJSON_GetArraySize(params) != 2)
    {
        context->error_code = JSON_RPC_CODE_INVALID_PARAMS;
        context->error_message = strdup("Invalid params: Expected two numbers.");
        return NULL;
    }

    int a = cJSON_GetArrayItem(params, 0)->valueint;
    int b = cJSON_GetArrayItem(params, 1)->valueint;

    cJSON* result = cJSON_CreateNumber(a + b);
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t, '0' means default
    mjrpc_handle_t* handle = mjrpc_create_handle(16);

    // Add a method
    mjrpc_add_method(handle, add, "add", NULL);

    // Construct a JSON-RPC request with parameters
    const char* json_request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[2, 3],\"id\":1}";

    // Process the request
    int result;
    char* json_response = mjrpc_process_str(handle, json_request, &result);

    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);
    // Assert that the response contains "5" (2+3=5)
    assert(json_response != NULL);

    // Show the response
    printf("Response: %s\n", json_response);
    free(json_response);

    // Cleanup
    mjrpc_destroy_handle(handle);

    // End of this example
    return 0;
}
