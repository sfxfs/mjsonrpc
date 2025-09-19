// mjsonrpc 数组参数与返回值相关单元测试
#include "unity.h"
#include "mjsonrpc.h"
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// 测试数组参数的加法
static cJSON* sum_array_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    if (!params || !cJSON_IsArray(params))
    {
        ctx->error_code = JSON_RPC_CODE_INVALID_PARAMS;
        ctx->error_message = strdup("params must be array");
        return NULL;
    }
    int sum = 0;
    int n = cJSON_GetArraySize(params);
    for (int i = 0; i < n; ++i)
    {
        cJSON* item = cJSON_GetArrayItem(params, i);
        if (!cJSON_IsNumber(item))
        {
            ctx->error_code = JSON_RPC_CODE_INVALID_PARAMS;
            ctx->error_message = strdup("array element not number");
            return NULL;
        }
        sum += item->valueint;
    }
    return cJSON_CreateNumber(sum);
}

void test_sum_array(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, sum_array_func, "sum_array", NULL);
    // 正常数组
    cJSON* arr = cJSON_CreateIntArray((int[]) {1, 2, 3, 4}, 4);
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("sum_array", arr, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result));
    TEST_ASSERT_EQUAL_INT(10, result->valueint);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

void test_sum_array_empty(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, sum_array_func, "sum_array", NULL);
    cJSON* arr = cJSON_CreateArray();
    cJSON* id = cJSON_CreateNumber(2);
    cJSON* req = mjrpc_request_cjson("sum_array", arr, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result));
    TEST_ASSERT_EQUAL_INT(0, result->valueint);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

void test_sum_array_type_error(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, sum_array_func, "sum_array", NULL);
    cJSON* arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("notnum"));
    cJSON* id = cJSON_CreateNumber(3);
    cJSON* req = mjrpc_request_cjson("sum_array", arr, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* error = cJSON_GetObjectItem(resp, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_EQUAL_INT(JSON_RPC_CODE_INVALID_PARAMS, code_item->valueint);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sum_array);
    RUN_TEST(test_sum_array_empty);
    RUN_TEST(test_sum_array_type_error);
    return UNITY_END();
}
