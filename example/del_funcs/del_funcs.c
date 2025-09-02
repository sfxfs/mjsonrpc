#include "mjsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

// Define a simple JSON-RPC method
cJSON* hello_world(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Hello, World!");
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t, '0' means default
    mjrpc_handle_t* handle = mjrpc_create_handle(0);

    // Add a method
    mjrpc_add_method(handle, hello_world, "hello", NULL);

    // Construct a JSON-RPC request
    const char* json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}";

    // Process the request
    int result;
    char* json_response = mjrpc_process_str(handle, json_request, &result);

    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);
    // Assert that the response contains "Goodbye, World!"
    assert(json_response != NULL);
    assert(strstr(json_response, "Hello, World!") != NULL);

    // Show the response
    printf("Response: %s\n", json_response);
    free(json_response);

    // Delete a method
    mjrpc_del_method(handle, "hello");

    // Process the request again
    json_response = mjrpc_process_str(handle, json_request, &result);

    // After deleting the method, calling it again should return an error (but result should still
    // be MJRPC_RET_OK, and the error is in json_response)
    assert(result == MJRPC_RET_OK);
    assert(json_response != NULL);
    assert(strstr(json_response, "Method not found") != NULL);

    // Show the response
    printf("Response: %s\n", json_response);
    free(json_response);

    // Cleanup
    mjrpc_destroy_handle(handle);

    // End of this example
    return 0;
}
