#include "unity.h"
#include "mjsonrpc.h"
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// 用于测试自动扩容的空方法
static cJSON* dummy_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id) {
    return cJSON_CreateString("ok");
}

void test_auto_resize(void) {
    size_t initial_capacity = 4;
    mjrpc_handle_t* h = mjrpc_create_handle(initial_capacity);
    // 注册比初始容量多的方法，触发扩容
    int method_count = 20;
    char name[32];
    for (int i = 0; i < method_count; ++i) {
        snprintf(name, sizeof(name), "m%d", i);
        int ret = mjrpc_add_method(h, dummy_func, name, NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }
    // 检查所有方法都能被正确调用
    for (int i = 0; i < method_count; ++i) {
        snprintf(name, sizeof(name), "m%d", i);
        cJSON* id = cJSON_CreateNumber(i);
        cJSON* req = mjrpc_request_cjson(name, NULL, id);
        int code = -1;
        cJSON* resp = mjrpc_process_cjson(h, req, &code);
        TEST_ASSERT_NOT_NULL(resp);
        cJSON* result = cJSON_GetObjectItem(resp, "result");
        TEST_ASSERT_TRUE(cJSON_IsString(result));
        TEST_ASSERT_EQUAL_STRING("ok", result->valuestring);
        cJSON_Delete(resp);
    }
    mjrpc_destroy_handle(h);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_auto_resize);
    return UNITY_END();
}
