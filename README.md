# mjsonrpc - A JSON-RPC 2.0 Message Parser and Generator Based on cJSON

![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/sfxfs/mjsonrpc/ci.yml) ![GitHub License](https://img.shields.io/github/license/sfxfs/mjsonrpc) ![GitHub commit activity](https://img.shields.io/github/commit-activity/t/sfxfs/mjsonrpc) ![GitHub top language](https://img.shields.io/github/languages/top/sfxfs/mjsonrpc) ![GitHub repo size](https://img.shields.io/github/repo-size/sfxfs/mjsonrpc) ![GitHub Downloads (all assets, latest release)](https://img.shields.io/github/downloads/sfxfs/mjsonrpc/latest/total)

[ English | [中文](README_CN.md) ]

## Introduction

This project is lightweight, has minimal dependencies, and can be integrated into various communication methods (TCP, UDP, message queues, etc.). It is simple to use (with only a few functional APIs) and has good performance (using hash-based indexing instead of polling all methods). It also supports batch calls (JSON Array), automatic generation of corresponding error messages or custom error messages based on requests, customizable memory management hooks, notification requests, and more.

## Features

- **Lightweight & Minimal Dependencies**: Only depends on cJSON
- **Hash-based Method Indexing**: Fast method lookup using double hashing algorithm
- **Batch Requests**: Support for JSON Array batch calls
- **Customizable Memory Management**: User-defined malloc/free/strdup hooks
- **Thread-Safe**: Thread-local storage for memory hooks
- **POSIX Array Params**: Support for both object and array parameters
- **Method Enumeration**: Query registered methods at runtime
- **Error Logging**: Optional error logging hooks for debugging

## How to Use

Simply add the project source files (**mjsonrpc.c**, **mjsonrpc.h**) and the cJSON library to your own project and compile them together. Alternatively, you can use CMake to build and install:

```bash
cmake -B build && cmake --build build
sudo cmake --install build
```

## Examples

### Basic Usage

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

### Method with Parameters

```c
cJSON *add(mjrpc_func_ctx_t *ctx, cJSON *params, cJSON *id) {
    int a = 0, b = 0;
    cJSON *a_item = cJSON_GetObjectItem(params, "a");
    cJSON *b_item = cJSON_GetObjectItem(params, "b");
    if (cJSON_IsNumber(a_item)) a = a_item->valueint;
    if (cJSON_IsNumber(b_item)) b = b_item->valueint;
    return cJSON_CreateNumber(a + b);
}

int main() {
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, add, "add", NULL);

    // Request with object parameters
    const char *req = "{\"jsonrpc\":\"2.0\",\"method\":\"add\","
                      "\"params\":{\"a\":5,\"b\":3},\"id\":1}";
    int code;
    char *resp = mjrpc_process_str(h, req, &code);
    // Response: {"jsonrpc":"2.0","result":8,"id":1}
    printf("Response: %s\n", resp);

    free(resp);
    mjrpc_destroy_handle(h);
    return 0;
}
```

### Batch Requests

```c
cJSON *multiply(mjrpc_func_ctx_t *ctx, cJSON *params, cJSON *id) {
    int a = cJSON_GetArrayItem(params, 0)->valueint;
    int b = cJSON_GetArrayItem(params, 1)->valueint;
    return cJSON_CreateNumber(a * b);
}

int main() {
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, multiply, "mul", NULL);

    // Batch request with multiple calls
    const char *batch_req = "["
        "{\"jsonrpc\":\"2.0\",\"method\":\"mul\",\"params\":[3,4],\"id\":1},"
        "{\"jsonrpc\":\"2.0\",\"method\":\"mul\",\"params\":[5,6],\"id\":2}"
    "]";

    int code;
    char *resp = mjrpc_process_str(h, batch_req, &code);
    // Response: [{"jsonrpc":"2.0","result":12,"id":1},{"jsonrpc":"2.0","result":30,"id":2}]
    printf("Batch Response: %s\n", resp);

    free(resp);
    mjrpc_destroy_handle(h);
    return 0;
}
```

### Custom Error Handling

```c
cJSON *validate_age(mjrpc_func_ctx_t *ctx, cJSON *params, cJSON *id) {
    cJSON *age = cJSON_GetObjectItem(params, "age");
    if (!cJSON_IsNumber(age) || age->valueint < 0 || age->valueint > 150) {
        ctx->error_code = -32001;  // Custom error code
        ctx->error_message = strdup("Invalid age: must be 0-150");
        return NULL;
    }
    return cJSON_CreateString("Age validated");
}
```

### Custom Memory Management

```c
void* my_malloc(size_t size) {
    printf("Allocating %zu bytes\n", size);
    return malloc(size);
}

void my_free(void* ptr) {
    printf("Freeing %p\n", ptr);
    free(ptr);
}

char* my_strdup(const char* str) {
    char* dup = my_malloc(strlen(str) + 1);
    if (dup) strcpy(dup, str);
    return dup;
}

int main() {
    // Set custom memory hooks
    mjrpc_set_memory_hooks(my_malloc, my_free, my_strdup);

    mjrpc_handle_t* h = mjrpc_create_handle(8);
    // ... use handle ...
    mjrpc_destroy_handle(h);

    // Reset to default
    mjrpc_set_memory_hooks(NULL, NULL, NULL);
    return 0;
}
```

### Error Logging

```c
void my_error_logger(const char* msg, int code) {
    fprintf(stderr, "[MJSONRPC ERROR] %s (code: %d)\n", msg, code);
}

int main() {
    // Enable error logging
    mjrpc_set_error_log_hook(my_error_logger);

    mjrpc_handle_t* h = mjrpc_create_handle(8);
    // ... errors will be logged automatically ...
    mjrpc_destroy_handle(h);

    // Disable error logging
    mjrpc_set_error_log_hook(NULL);
    return 0;
}
```

## Performance

### Hash Table Performance Comparison

| Operation | Quadratic Probing | Double Hashing (new) |
|-----------|------------------:|---------------------:|
| Avg probes (low load) | ~1.5 | ~1.2 |
| Avg probes (high load) | ~4.0 | ~2.0 |
| Worst case | O(n) | O(n) |

### Method Registration Throughput

| Methods | Add (ops/sec) | Delete (ops/sec) | Lookup (ops/sec) |
|--------:|--------------:|-----------------:|-----------------:|
| 100     | ~500K         | ~600K            | ~800K            |
| 1000    | ~450K         | ~550K            | ~750K            |
| 10000   | ~400K         | ~500K            | ~700K            |

*Tested on Intel i7, 3.2GHz, single-threaded*

## FAQ

### Q: Is mjsonrpc thread-safe?

**A:** The library uses thread-local storage for memory hooks, making it safe to use from multiple threads. However, you should protect shared `mjrpc_handle_t` instances with your own synchronization (mutexes) if accessed from multiple threads concurrently.

### Q: How do I handle notification requests (no response)?

**A:** Notification requests are those without an `id` field. The library processes them normally but returns NULL for the response. Check the return code for `MJRPC_RET_OK_NOTIFICATION`.

```c
const char *notif = "{\"jsonrpc\":\"2.0\",\"method\":\"log\",\"params\":{\"msg\":\"hello\"}}";
int code;
char *resp = mjrpc_process_str(h, notif, &code);
if (code == MJRPC_RET_OK_NOTIFICATION) {
    printf("Notification processed (no response)\n");
}
```

### Q: Can I use array parameters instead of object parameters?

**A:** Yes! Both object and array parameters are supported:

```c
// Array parameters
const char *req = "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[1,2],\"id\":1}";
```

### Q: How do I enumerate all registered methods?

**A:** Use `mjrpc_enum_methods()` to get an array of method names:

```c
cJSON *methods = mjrpc_enum_methods(handle, NULL, NULL);
// methods is a JSON array of method name strings
cJSON_Delete(methods);
```

### Q: What happens if the hash table needs to resize?

**A:** The library automatically resizes the hash table when the load factor exceeds 0.75. This is transparent to the user. The resize operation uses double hashing for rehashing, which provides better distribution.

### Q: How do I clean up resources properly?

**A:** Always call `mjrpc_destroy_handle()` when done. This frees all internally allocated memory. Any cJSON objects returned to you should be freed with `cJSON_Delete()` or `free()` as documented.

## References

- [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)
- [JSON-RPC Specification](https://www.jsonrpc.org/specification)
- [hmng/jsonrpc-c](https://github.com/hmng/jsonrpc-c)
- [Doxygen Documentation](https://sfxfs.github.io/mjsonrpc)
