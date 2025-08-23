# mjsonrpc - A JSON-RPC 2.0 Message Parser and Generator Based on cJSON

English | [中文](README_CN.md)

### Introduction

This library is suitable for embedded systems and can be integrated into various communication methods (TCP, UDP, serial port). It is simple to use (only a few functions) and supports batch calls (JSON Array), automatic generation of corresponding error messages according to requests or generating customized error messages, as well as notification requests.

### Functions

1. Construct a Normal Response Object

    ```c
    cJSON *mjrpc_response_ok(cJSON *result, cJSON *id);
    ```

2. Construct an Error Response Object

    ```c
    cJSON *mjrpc_response_error(int code, char *message, cJSON *id);
    ```

3. Add a Method

    ```c
    int mjrpc_add_method(mjrpc_handle_t *handle,
                         mjrpc_func function_pointer,
                         char *method_name, void *arg2func);
    ```

4. Delete a Method

    ```c
    int mjrpc_del_method(mjrpc_handle_t *handle, char *method_name);
    ```

5. Process Request String

    ```c
    char *mjrpc_process_str(mjrpc_handle_t *handle,
                            const char *request_str,
                            int *ret_code);
    ```

6. Process Request cJSON Structure

    ```c
    cJSON *mjrpc_process_cjson(mjrpc_handle_t *handle,
                               cJSON *request_cjson,
                               int *ret_code);
    ```

### Example

```c
#include <stdio.h>
#include <stdlib.h>
#include "mjsonrpc.h"

// Define a simple JSON-RPC method
cJSON *hello_world(mjrpc_ctx_t *context, cJSON *params, cJSON *id)
{
    cJSON *result = cJSON_CreateString("Hello, World!");
    return result;
}

int main()
{
    // Initialize mjrpc_handle_t
    mjrpc_handle_t handle = {0};

    // Add a method
    mjrpc_add_method(&handle, hello_world, "hello", NULL);

    // Construct a JSON-RPC request
    const char *json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}";
    char *json_response = NULL;

    // Process the request
    int result;
    json_response = mjrpc_process_str(&handle, json_request, &result);

    if (result != MJRPC_RET_OK)
    {
        printf("Error processing request: %d\n", result);
    }

    if (json_response)
    {
        printf("Response: %s\n", json_response);
        free(json_response);
    }

    // Cleanup
    mjrpc_del_method(&handle, "hello");

    return 0;
}
```

### References

[DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)

[JSON-RPC Specification](https://www.jsonrpc.org/specification)

[hmng/jsonrpc-c](https://github.com/hmng/jsonrpc-c)
