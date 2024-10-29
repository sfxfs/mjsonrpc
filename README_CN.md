# mjsonrpc - 一个基于 cJSON 的 JSON-RPC 2.0 轻量服务端

### 介绍

适用于嵌入式系统，可以用于各种通信方式（TCP、UDP、串口），使用简单（只有三个功能函数），并支持批量调用以及自定义的错误信息等功能。

### 函数

1. 添加回调函数

```c
int mjrpc_add_method(mjrpc_handle_t *handle,
                     mjrpc_function function_pointer,
                     char *name, void *data);
```

2. 删除回调函数

```c
int mjrpc_del_method(mjrpc_handle_t *handle, char *name);
```

3. 字符串解析并调用对应函数

```c
int mjrpc_process(mjrpc_handle_t *handle,
                  const char *json_reqeust,
                  char **json_return_ptr);
```

### 示例

```c
#include "mjsonrpc.h"

mjrpc_handle_t handle; // 需要被初始化为 0

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
                     subtract_handler,	// 回调函数指针
                     "subtract",	// 方法名
                     NULL);	// 传入回调函数的参数
    ret_code = mjrpc_process(&handle,
                             jsonrpc_request_str,	// jsonrpc 请求字符串
                             &ret_str);	// 返回 json 字符串结果

    printf("return code: %d, return str: %s\n", ret_code, ret_str);

    free(ret_str);	// 不要忘记释放内存
    return 0;
}
```

