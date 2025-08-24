#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "mjsonrpc.h"

// Define a simple JSON-RPC method
cJSON* notify_func(mjrpc_ctx_t* context, cJSON* params, cJSON* id)
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
    mjrpc_handle_t handle = {0};
    mjrpc_add_method(&handle, notify_func, "notify_method", NULL);

    // Construct a JSON-RPC notification (without "id" field)
    const char* json_notify =
        "{\"jsonrpc\":\"2.0\",\"method\":\"notify_method\",\"params\":\"hello notify!\"}";
    int result;
    char* json_response = mjrpc_process_str(&handle, json_notify, &result);

    // Notification type should return NULL, and result == MJRPC_RET_OK_NOTIFICATION
    assert(json_response == NULL);
    assert(result == MJRPC_RET_OK_NOTIFICATION);
    printf("notify request passed!\n");

    mjrpc_del_method(&handle, "notify_method");
    return 0;
}
