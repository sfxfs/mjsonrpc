#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mjsonrpc.h"

// Define a simple JSON-RPC method
cJSON* Count(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Invoked!");
    (*(int*) context->data)++;
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t
    mjrpc_handle_t* handle = mjrpc_create_handle(16);

    int* count = malloc(sizeof(int));
    *count = 0;
    // Add a method
    mjrpc_add_method(handle, Count, "hello", count);

    // Construct a JSON-RPC request
    const char* json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}";
    char* json_response = NULL;

    // Process the request
    int result;

    json_response = mjrpc_process_str(handle, json_request, &result);
    assert(*count == 1);
    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);
    // Assert that the response contains "Hello, World!"
    assert(json_response != NULL);
    printf("Response: %s\n", json_response);
    free(json_response);

    // Cleanup
    mjrpc_del_method(handle, "hello");

    mjrpc_destroy_handle(handle);

    return 0;
}
