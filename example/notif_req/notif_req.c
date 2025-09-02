#include "mjsonrpc.h"

#include <stdio.h>
#include <assert.h>

// Define a simple JSON-RPC method
cJSON* notify_func(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    printf("notify_func called!\n");
    if (params && cJSON_IsString(params))
    {
        printf("params: %s\n", params->valuestring);
    }
    return NULL; // No need to return
}

int main()
{
    // Initialize mjrpc_handle_t, '0' means default
    mjrpc_handle_t* handle = mjrpc_create_handle(16);

    // Add a method
    mjrpc_add_method(handle, notify_func, "notify_method", NULL);

    // Construct a JSON-RPC notification (without "id" field)
    const char* json_notify =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_method\",\"params\":\"hello notify!\"}";

    // Process the request
    int result;
    char* json_response = mjrpc_process_str(handle, json_notify, &result);

    // Notification type should return NULL, and result == MJRPC_RET_OK_NOTIFICATION
    assert(json_response == NULL);
    assert(result == MJRPC_RET_OK_NOTIFICATION);
    printf("notify request passed!\n");

    // clean up
    mjrpc_destroy_handle(handle);

    // End of this example
    return 0;
}
