# mjsonrpc - A JSON-RPC 2.0 Lightweight Server based on cJSON

[中文](README_CN.md)

### Introduction

Designed for embedded systems, it can be used for various communication methods (TCP, UDP, serial), with simple usage (only three function functions), and supports batch calls and custom error messages.

### Functions

1. Add callback function

```c
int mjrpc_add_method(mjrpc_handle_t *handle,
                     mjrpc_function function_pointer,
                     char *name, void *data);
```

2. Delete callback function

```c
int mjrpc_del_method(mjrpc_handle_t *handle, char *name);
```

3. Parse strings and call corresponding functions

```c
int mjrpc_process(mjrpc_handle_t *handle,
                  const char *json_request,
                  char **json_return_ptr);
```

### Example

```c
#include "mjsonrpc.h"

mjrpc_handle_t handle; // Should be initialized to 0

const char jsonrpc_request_str[] = "{\"jsonrpc\": \"2.0\", \"method\": \"subtract\", \"params\": [42, 23], \"id\": 1}";

cJSON *subtract_handler(mjrpc_ctx_t *ctx, cJSON *params, cJSON *id)
{
    //...
}

int main()
{
    char *ret_str;
    int ret_code;

    mjrpc_add_method(&handle,
                     subtract_handler, // Callback function pointer
                     "subtract", // Method name
                     NULL); // Parameters passed to the callback function
    ret_code = mjrpc_process(&handle,
                             jsonrpc_request_str, // JSON-RPC request string
                             &ret_str); // Return JSON string result

    printf("return code: %d, return str: %s\n", ret_code, ret_str);

    free(ret_str); // Don't forget to free memory
    return 0;
}
```

