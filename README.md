# mjsonrpc - A JSON-RPC 2.0 Message Parser and Generator Based on cJSON

![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/sfxfs/mjsonrpc/ci.yml) ![GitHub License](https://img.shields.io/github/license/sfxfs/mjsonrpc) ![GitHub commit activity](https://img.shields.io/github/commit-activity/t/sfxfs/mjsonrpc) ![GitHub top language](https://img.shields.io/github/languages/top/sfxfs/mjsonrpc) ![GitHub repo size](https://img.shields.io/github/repo-size/sfxfs/mjsonrpc) ![GitHub Downloads (all assets, latest release)](https://img.shields.io/github/downloads/sfxfs/mjsonrpc/latest/total)

[ English | [中文](README_CN.md) ]

### Introduction

This project is lightweight, has minimal dependencies, and can be integrated into various communication methods (TCP, UDP, message queues, etc.). It is simple to use (with only a few functional APIs) and has good performance (using hash-based indexing instead of polling all methods). It also supports batch calls (JSON Array), automatic generation of corresponding error messages or custom error messages based on requests, customizable memory management hooks, and notification requests.

### How to Use

Simply add the project source files (**mjsonrpc.c**, **mjsonrpc.h**) and the cJSON library to your own project and compile them together. Alternatively, you can compile them into a dynamic library and link it.

### Function Definitions

For detailed API descriptions, please refer to **src/mjsonrpc.h** and [Doxygen docs of this repo](https://sfxfs.github.io/mjsonrpc).

### Example

```c
#include "mjsonrpc.h"
#include <stdio.h>
#include <stdlib.h>

// Define a simple JSON-RPC method
cJSON *hello_world(mjrpc_func_ctx_t *context, cJSON *params, cJSON *id) {
    cJSON *result = cJSON_CreateString("Hello, World!");
    return result;
}

int main() {
    // Initialize mjrpc_handle_t
    mjrpc_handle_t* handle = mjrpc_create_handle(0);

    // Add a method
    mjrpc_add_method(handle, hello_world, "hello", NULL);

    // Construct a JSON-RPC request
    const char *json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}";

    // Process the request
    int result;
    char *json_response = mjrpc_process_str(handle, json_request, &result);

    // Check return code
    if (result != MJRPC_RET_OK) {
        printf("Error processing request: %d\n", result);
    }

    // Check response string
    if (json_response) {
        printf("Response: %s\n", json_response);
        free(json_response);
    }

    // Cleanup
    mjrpc_destroy_handle(handle);

    return 0;
}
```

### References

- [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)
- [JSON-RPC Specification](https://www.jsonrpc.org/specification)
- [hmng/jsonrpc-c](https://github.com/hmng/jsonrpc-c)
