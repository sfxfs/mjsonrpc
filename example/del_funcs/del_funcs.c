#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mjsonrpc.h"

// Define a simple JSON-RPC method
cJSON* hello_world(mjrpc_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Hello, World!");
    return result;
}

// Define another simple JSON-RPC method
cJSON* goodbye_world(mjrpc_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Goodbye, World!");
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
    const char* json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":1}";
    int result;

    // Process the request

    char* json_response = mjrpc_process_str(&handle, json_request, &result);
    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);
    // Assert that the response contains "Goodbye, World!"
    assert(json_response != NULL);
    printf("Response: %s\n", json_response);
    assert(strstr(json_response, "Goodbye, World!") != NULL);
    free(json_response);

    // Delete a method
    mjrpc_del_method(&handle, "goodbye");

    // Attempt to call the deleted method
    json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":1}";

    json_response = mjrpc_process_str(&handle, json_request, &result);
    // After deleting the method, calling it again should return an error (but result should still
    // be MJRPC_RET_OK, and the error is in json_response)
    assert(result == MJRPC_RET_OK);
    assert(json_response != NULL);
    printf("Response: %s\n", json_response);
    // Assert that the response contains "Method not found" (jsonrpc standard error)
    assert(strstr(json_response, "Method not found") != NULL);
    free(json_response);

    // Cleanup
    mjrpc_del_method(&handle, "hello");

    return 0;
}
