#include "mjsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Define a JSON-RPC method with custom error handling
cJSON* divide(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    if (params == NULL || cJSON_GetArraySize(params) != 2)
    {
        context->error_code = JSON_RPC_CODE_INVALID_PARAMS;
        context->error_message = strdup("Invalid params: Expected two numbers.");
        return NULL;
    }

    int a = cJSON_GetArrayItem(params, 0)->valueint;
    int b = cJSON_GetArrayItem(params, 1)->valueint;

    if (b == 0)
    {
        context->error_code = -32000; // Custom error code
        context->error_message = strdup("Division by zero is not allowed.");
        return NULL;
    }

    cJSON* result = cJSON_CreateNumber((double) a / b);
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t, '0' means default
    mjrpc_handle_t* handle = mjrpc_create_handle(0);

    // Add a method
    mjrpc_add_method(handle, divide, "divide", NULL);

    // Construct a JSON-RPC request with valid parameters
    const char* json_request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"divide\",\"params\":[10, 2],\"id\":1}";

    // Process the request
    int result;
    char* json_response = mjrpc_process_str(handle, json_request, &result);

    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);
    // Assert that the response is not NULL and contains 5 (10/2=5)
    assert(json_response != NULL);

    // Show the response
    printf("Response: %s\n", json_response);
    free(json_response);

    // Construct a JSON-RPC request with invalid parameters (division by zero)
    json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"divide\",\"params\":[10, 0],\"id\":2}";

    // Process the request
    json_response = mjrpc_process_str(handle, json_request, &result);

    // Assert that the result must be MJRPC_RET_OK (even if there is an error, the jsonrpc protocol
    // should return an error object)
    assert(result == MJRPC_RET_OK);
    // Assert that the response contains "Division by zero is not allowed."
    assert(json_response != NULL);
    assert(strstr(json_response, "Division by zero is not allowed") != NULL);

    // Show the response
    printf("Response: %s\n", json_response);
    free(json_response);

    // Cleanup
    mjrpc_destroy_handle(handle);

    // End of this example
    return 0;
}
