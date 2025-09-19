
#include "unity.h"
#include "mjsonrpc.h"
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// 简单加法方法
static cJSON* add_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id) {
    int a = 0, b = 0;
    if (params && cJSON_IsArray(params) && cJSON_GetArraySize(params) == 2) {
        cJSON* a_item = cJSON_GetArrayItem(params, 0);
        cJSON* b_item = cJSON_GetArrayItem(params, 1);
        if (cJSON_IsNumber(a_item) && cJSON_IsNumber(b_item)) {
            a = a_item->valueint;
            b = b_item->valueint;
        }
    }
    return cJSON_CreateNumber(a + b);
}

// 批量请求正常
void test_batch_request_normal(void) {
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, add_func, "add", NULL);
    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < 3; ++i) {
        cJSON* params = cJSON_CreateIntArray((int[]){i, i+1}, 2);
        cJSON* id = cJSON_CreateNumber(i+1);
        cJSON* req = mjrpc_request_cjson("add", params, id);
        cJSON_AddItemToArray(arr, req);
    }
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, arr, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_TRUE(cJSON_IsArray(resp));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(resp));
    for (int i = 0; i < 3; ++i) {
        cJSON* item = cJSON_GetArrayItem(resp, i);
        cJSON* result = cJSON_GetObjectItem(item, "result");
        TEST_ASSERT_TRUE(cJSON_IsNumber(result));
        TEST_ASSERT_EQUAL_INT(i + (i+1), result->valueint);
    }
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

// 批量请求部分错误
void test_batch_request_partial_error(void) {
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, add_func, "add", NULL);
    cJSON* arr = cJSON_CreateArray();
    // 正常
    cJSON* req1 = mjrpc_request_cjson("add", cJSON_CreateIntArray((int[]){1,2},2), cJSON_CreateNumber(1));
    // 错误方法
    cJSON* req2 = mjrpc_request_cjson("no_such", NULL, cJSON_CreateNumber(2));
    // 正常
    cJSON* req3 = mjrpc_request_cjson("add", cJSON_CreateIntArray((int[]){3,4},2), cJSON_CreateNumber(3));
    cJSON_AddItemToArray(arr, req1);
    cJSON_AddItemToArray(arr, req2);
    cJSON_AddItemToArray(arr, req3);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, arr, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_TRUE(cJSON_IsArray(resp));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(resp));
    // 检查第2项为 error
    cJSON* item2 = cJSON_GetArrayItem(resp, 1);
    cJSON* error = cJSON_GetObjectItem(item2, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_EQUAL_INT(JSON_RPC_CODE_METHOD_NOT_FOUND, code_item->valueint);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

// 空数组请求
void test_batch_request_empty_array(void) {
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    cJSON* arr = cJSON_CreateArray();
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, arr, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* error = cJSON_GetObjectItem(resp, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_EMPTY_REQUEST, code);
    TEST_ASSERT_EQUAL_INT(JSON_RPC_CODE_PARSE_ERROR, code_item->valueint);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_batch_request_normal);
    RUN_TEST(test_batch_request_partial_error);
    RUN_TEST(test_batch_request_empty_array);
    return UNITY_END();
}
