#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mjsonrpc.h"

// Define a JSON-RPC method with parameters
cJSON* add(mjrpc_ctx_t* context, cJSON* params, cJSON* id)
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
    // Initialize mjrpc_handle_t
    mjrpc_handle_t handle = {0};

    // Add a method
    mjrpc_add_method(&handle, add, "add", NULL);

    // Construct a JSON-RPC request with parameters
    const char* json_request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[2, 3],\"id\":1}";
    int result;

    // Process the request

    char* json_response = mjrpc_process_str(&handle, json_request, &result);
    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);
    // Assert that the response contains "5" (2+3=5)
    assert(json_response != NULL);
    printf("Response: %s\n", json_response);
    assert(strstr(json_response, "5") != NULL);
    free(json_response);

    // Cleanup
    mjrpc_del_method(&handle, "add");

    return 0;
}
