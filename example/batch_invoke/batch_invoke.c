#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mjsonrpc.h"

static int count = 0;

// Define a simple JSON-RPC method
cJSON* hello_world(mjrpc_ctx_t* context, cJSON* params, cJSON* id)
{
    cJSON* result = cJSON_CreateString("Hello, World!");
    count++;
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

    // Construct a batch JSON-RPC request，include a notification (No "id" memeber)
    const char* json_request = "["
                               "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1},"
                               "{\"jsonrpc\":\"2.0\",\"method\":\"goodbye\",\"id\":2},"
                               "{\"jsonrpc\":\"2.0\",\"method\":\"hello\"}"
                               "]";

    // Process the request
    int result;
    char* json_response = mjrpc_process_str(&handle, json_request, &result);

    // Assert that the result must be MJRPC_RET_OK
    assert(result == MJRPC_RET_OK);

    // Assert that the response is not NULL
    assert(json_response != NULL);

    // Assert that the response contains "Hello, World!" and "Goodbye, World!"
    printf("Response: %s\n", json_response);
    assert(strstr(json_response, "Hello, World!") != NULL);
    assert(strstr(json_response, "Goodbye, World!") != NULL);
    assert(count == 2); // hello_world should be called twice
    free(json_response);

    // Cleanup
    mjrpc_del_method(&handle, "hello");
    mjrpc_del_method(&handle, "goodbye");

    return 0;
}
