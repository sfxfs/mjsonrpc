#include "unity.h"
#include "mjsonrpc.h"
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// 测试对象参数的字段累加
static cJSON* sum_obj_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    int a = 0, b = 0;
    cJSON* a_item = cJSON_GetObjectItem(params, "a");
    cJSON* b_item = cJSON_GetObjectItem(params, "b");
    if (cJSON_IsNumber(a_item))
        a = a_item->valueint;
    if (cJSON_IsNumber(b_item))
        b = b_item->valueint;
    return cJSON_CreateNumber(a + b);
}

void test_sum_obj(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, sum_obj_func, "sum_obj", NULL);
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "a", 7);
    cJSON_AddNumberToObject(obj, "b", 8);
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("sum_obj", obj, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result));
    TEST_ASSERT_EQUAL_INT(15, result->valueint);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

// 测试嵌套对象参数
static cJSON* nested_obj_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    cJSON* inner = cJSON_GetObjectItem(params, "inner");
    cJSON* x = cJSON_GetObjectItem(inner, "x");
    cJSON* y = cJSON_GetObjectItem(inner, "y");
    int sum = 0;
    if (cJSON_IsNumber(x))
        sum += x->valueint;
    if (cJSON_IsNumber(y))
        sum += y->valueint;
    return cJSON_CreateNumber(sum);
}

void test_nested_obj(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, nested_obj_func, "nested_obj", NULL);
    cJSON* obj = cJSON_CreateObject();
    cJSON* inner = cJSON_CreateObject();
    cJSON_AddNumberToObject(inner, "x", 3);
    cJSON_AddNumberToObject(inner, "y", 4);
    cJSON_AddItemToObject(obj, "inner", inner);
    cJSON* id = cJSON_CreateNumber(2);
    cJSON* req = mjrpc_request_cjson("nested_obj", obj, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result));
    TEST_ASSERT_EQUAL_INT(7, result->valueint);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

void test_del_method(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    // 添加并删除
    mjrpc_add_method(h, sum_obj_func, "sum_obj", NULL);
    int ret = mjrpc_del_method(h, "sum_obj");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    // 再次删除应返回 NOT_FOUND
    ret = mjrpc_del_method(h, "sum_obj");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_NOT_FOUND, ret);
    // 删除不存在的方法
    ret = mjrpc_del_method(h, "no_such");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_NOT_FOUND, ret);
    // 删除空方法名
    ret = mjrpc_del_method(h, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_INVALID_PARAM, ret);
    mjrpc_destroy_handle(h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sum_obj);
    RUN_TEST(test_nested_obj);
    RUN_TEST(test_del_method);
    return UNITY_END();
}
