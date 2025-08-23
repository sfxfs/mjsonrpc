# mjsonrpc - 一个基于 cJSON 的 JSON-RPC 2.0 最简服务端中间层

### 介绍

该库适用于各种嵌入式系统，可以集成进各种通信方式（TCP、UDP、串口），使用简单（只有少量功能函数），并支持批量调用 (JSON Array) 、自动根据请求生成对应错误信息或产生自定义的错误信息，以及通知请求等功能。

### 函数

1. 构造正常响应对象

```c
cJSON *mjrpc_response_ok(cJSON *result, cJSON *id);
```

2. 构造错误响应对象

```c
cJSON *mjrpc_response_error(int code, char *message, cJSON *id);
```

3. 添加方法

```c
int mjrpc_add_method(mjrpc_handle_t *handle,
                     mjrpc_func function_pointer,
                     char *method_name, void *arg2func);
```

4. 删除方法

```c
int mjrpc_del_method(mjrpc_handle_t *handle, char *method_name);
```

5. 处理请求字符串

```c
char *mjrpc_process_str(mjrpc_handle_t *handle,
                        const char *reqeust_str,
                        int *ret_code);
```

6. 处理请求 cJSON 结构体

```c
cJSON *mjrpc_process_cjson(mjrpc_handle_t *handle,
                           cJSON *request_cjson,
                           int *ret_code);
```

### 示例

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

### 参考

[JSON-RPC Official Docs](https://www.jsonrpc.org/specification)

[jsonrpc-c](https://github.com/hmng/jsonrpc-c)
