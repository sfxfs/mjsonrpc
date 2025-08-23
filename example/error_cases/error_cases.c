#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mjsonrpc.h"

// A valid method for testing
cJSON* echo(mjrpc_ctx_t* context, cJSON* params, cJSON* id)
{
    if (!params || !cJSON_IsString(params))
    {
        context->error_code = JSON_RPC_CODE_INVALID_PARAMS;
        context->error_message = strdup("params must be a string");
        return NULL;
    }
    return cJSON_Duplicate(params, 1);
}

int main()
{
    mjrpc_handle_t handle = {0};
    mjrpc_add_method(&handle, echo, "echo", NULL);
    int result;
    char* response;

    // 1. Parse error (invalid JSON)
    response = mjrpc_process_str(&handle, "{invalid json}", &result);
    assert(result != MJRPC_RET_OK);
    printf("Parse error: %s\n", response);
    free(response);

    // 2. Invalid request (not an object or array)
    response = mjrpc_process_str(&handle, "123", &result);
    assert(result != MJRPC_RET_OK);
    printf("Invalid request: %s\n", response);
    free(response);

    // 3. Method not found
    response = mjrpc_process_str(&handle, "{\"jsonrpc\":\"2.0\",\"method\":\"not_exist\",\"id\":1}",
                                 &result);
    assert(result == MJRPC_RET_OK);
    printf("Method not found: %s\n", response);
    free(response);

    // 4. Invalid params
    response = mjrpc_process_str(
        &handle, "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"params\":123,\"id\":2}", &result);
    assert(result == MJRPC_RET_OK);
    printf("Invalid params: %s\n", response);
    free(response);

    // 5. JSONRPC version error
    response = mjrpc_process_str(
        &handle, "{\"jsonrpc\":\"1.0\",\"method\":\"echo\",\"params\":\"hi\",\"id\":3}", &result);
    assert(result == MJRPC_RET_OK);
    printf("Version error: %s\n", response);
    free(response);

    // 6. No method member
    response = mjrpc_process_str(&handle, "{\"jsonrpc\":\"2.0\",\"id\":4}", &result);
    assert(result == MJRPC_RET_OK);
    printf("No method: %s\n", response);
    free(response);

    // 7. id type error
    response =
        mjrpc_process_str(&handle, "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"id\":{}}", &result);
    assert(result == MJRPC_RET_OK);
    printf("id type error: %s\n", response);
    free(response);
    mjrpc_del_method(&handle, "echo");
    return 0;
}
