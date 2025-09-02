#include "mjsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Define a simple JSON-RPC method
cJSON* function(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Function invoked!");
    (*(int*) context->data)++;
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t, '0' means default
    mjrpc_handle_t* handle = mjrpc_create_handle(0);

    // allocate an argument, all the arg into method should have their space in heap mem
    int* count = malloc(sizeof(int));
    *count = 0;

    // Add a method
    mjrpc_add_method(handle, function, "func", count);

    // Construct a JSON-RPC request
    const char* json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"func\",\"id\":1}";

    // Process the request
    int result;
    char* json_response = mjrpc_process_str(handle, json_request, &result);

    // result check
    assert(*count == 1);
    assert(result == MJRPC_RET_OK);
    assert(json_response != NULL);

    // Show the response
    printf("Response: %s\n", json_response);
    free(json_response);

    // Cleanup
    mjrpc_destroy_handle(handle);

    // End of this example
    return 0;
}
