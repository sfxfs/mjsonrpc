# mjsonrpc - 基于 C 语言的 JSON-RPC 2.0 的服务端消息解析器和生成器

![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/sfxfs/mjsonrpc/ci.yml) ![GitHub License](https://img.shields.io/github/license/sfxfs/mjsonrpc) ![GitHub commit activity](https://img.shields.io/github/commit-activity/t/sfxfs/mjsonrpc) ![GitHub top language](https://img.shields.io/github/languages/top/sfxfs/mjsonrpc) ![GitHub repo size](https://img.shields.io/github/repo-size/sfxfs/mjsonrpc) ![GitHub Downloads (all assets, latest release)](https://img.shields.io/github/downloads/sfxfs/mjsonrpc/latest/total)

### 介绍

本项目轻量，依赖少，可以集成进各种通信方式（TCP、UDP、消息队列等），使用简单（只有少量功能函数），性能好（哈希值计算索引，无需轮询所有方法），并支持批量调用 (JSON Array) 、自动根据请求生成对应错误信息或构造自定义的错误信息，以及通知请求等功能。

### 如何使用

将项目源文件（`mjsonrpc.c`、`mjsonrpc.h`）和 `cJSON` 库加入自己的工程一同编译即可，也可以自行编译为动态库进行链接或在本仓库 [releases](https://github.com/sfxfs/mjsonrpc/releases) 中下载已经编译好的二进制版本。

### 函数定义

详细 API 描述请见 `src/mjsonrpc.h`，或 [本仓库 Doxygen 文档](https://sfxfs.github.io/mjsonrpc)。

### 示例

```c
#include "mjsonrpc.h"
#include <stdio.h>
#include <stdlib.h>

// 定义一个简单的 JSON-RPC 方法
cJSON *hello_world(mjrpc_func_ctx_t *context, cJSON *params, cJSON *id) {
    cJSON *result = cJSON_CreateString("Hello, World!");
    return result;
}

int main() {
    // 初始化 mjrpc_handle_t
    mjrpc_handle_t* handle = mjrpc_create_handle(0);

    // 添加一个方法
    mjrpc_add_method(handle, hello_world, "hello", NULL);

    // 生成 JSON-RPC 请求
    const char *json_request = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}";

    // 处理请求
    int result;
    char *json_response = mjrpc_process_str(handle, json_request, &result);

  	// 检查返回值
    if (result != MJRPC_RET_OK) {
        printf("Error processing request: %d\n", result);
    }

  	// 检查响应字符串
    if (json_response) {
        printf("Response: %s\n", json_response);
        free(json_response);
    }

    // 释放句柄
    mjrpc_destroy_handle(handle);

    return 0;
}
```

### 参考

- [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)

- [JSON-RPC Specification](https://www.jsonrpc.org/specification)

- [hmng/jsonrpc-c](https://github.com/hmng/jsonrpc-c)
