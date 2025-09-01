#include <string.h>
#include <stdlib.h>

#include "unity.h"
#include "mjsonrpc.h"

void setUp(void) {}
void tearDown(void) {}

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

void test_mjrpc_add_and_call(void) {
    mjrpc_handle_t* handle = mjrpc_create_handle(16);
    const int ret = mjrpc_add_method(handle, add_func, "add", NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    const char* req = "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[3,5],\"id\":1}";
    int code = -1;
    char* resp = mjrpc_process_str(handle, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

    cJSON* resp_json = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(resp_json);
    const cJSON* result = cJSON_GetObjectItem(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(cJSON_IsNumber(result));
    TEST_ASSERT_EQUAL_INT(8, result->valueint);

    cJSON_Delete(resp_json);
    free(resp);
    mjrpc_destroy_handle(handle);
}


static cJSON* mul_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id) {
    int a = 1, b = 1;
    if (params && cJSON_IsArray(params) && cJSON_GetArraySize(params) == 2) {
        cJSON* a_item = cJSON_GetArrayItem(params, 0);
        cJSON* b_item = cJSON_GetArrayItem(params, 1);
        if (cJSON_IsNumber(a_item) && cJSON_IsNumber(b_item)) {
            a = a_item->valueint;
            b = b_item->valueint;
        }
    }
    return cJSON_CreateNumber(a * b);
}

void test_method_not_found(void) {
    mjrpc_handle_t* handle = mjrpc_create_handle(16);
    const char* req = "{\"jsonrpc\":\"2.0\",\"method\":\"no_such\",\"id\":2}";
    int code = -1;
    char* resp = mjrpc_process_str(handle, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* resp_json = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(resp_json);
    cJSON* error = cJSON_GetObjectItem(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_TRUE(cJSON_IsNumber(code_item));
    TEST_ASSERT_EQUAL_INT(JSON_RPC_CODE_METHOD_NOT_FOUND, code_item->valueint);
    cJSON_Delete(resp_json);
    free(resp);
    mjrpc_destroy_handle(handle);
}

void test_invalid_params(void) {
    mjrpc_handle_t* handle = mjrpc_create_handle(16);
    mjrpc_add_method(handle, add_func, "add", NULL);
    // 参数数量错误
    const char* req = "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[1],\"id\":3}";
    int code = -1;
    char* resp = mjrpc_process_str(handle, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* resp_json = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(resp_json);
    cJSON* result = cJSON_GetObjectItem(resp_json, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result));
    TEST_ASSERT_EQUAL_INT(0, result->valueint);
    cJSON_Delete(resp_json);
    free(resp);
    mjrpc_destroy_handle(handle);
}

void test_invalid_request(void) {
    mjrpc_handle_t* handle = mjrpc_create_handle(16);
    // 缺少 method 字段
    const char* req = "{\"jsonrpc\":\"2.0\",\"id\":4}";
    int code = -1;
    char* resp = mjrpc_process_str(handle, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* resp_json = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(resp_json);
    cJSON* error = cJSON_GetObjectItem(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_TRUE(cJSON_IsNumber(code_item));
    TEST_ASSERT_EQUAL_INT(JSON_RPC_CODE_INVALID_REQUEST, code_item->valueint);
    cJSON_Delete(resp_json);
    free(resp);
    mjrpc_destroy_handle(handle);
}

void test_batch_request(void) {
    mjrpc_handle_t* handle = mjrpc_create_handle(16);
    mjrpc_add_method(handle, add_func, "add", NULL);
    mjrpc_add_method(handle, mul_func, "mul", NULL);
    const char* req = "[\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[2,3],\"id\":1},\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"mul\",\"params\":[2,3],\"id\":2}\n]";
    int code = -1;
    char* resp = mjrpc_process_str(handle, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* resp_json = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(resp_json);
    TEST_ASSERT_TRUE(cJSON_IsArray(resp_json));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(resp_json));
    cJSON* item1 = cJSON_GetArrayItem(resp_json, 0);
    cJSON* item2 = cJSON_GetArrayItem(resp_json, 1);
    TEST_ASSERT_NOT_NULL(item1);
    TEST_ASSERT_NOT_NULL(item2);
    cJSON* result1 = cJSON_GetObjectItem(item1, "result");
    cJSON* result2 = cJSON_GetObjectItem(item2, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result1));
    TEST_ASSERT_TRUE(cJSON_IsNumber(result2));
    TEST_ASSERT_EQUAL_INT(5, result1->valueint);
    TEST_ASSERT_EQUAL_INT(6, result2->valueint);
    cJSON_Delete(resp_json);
    free(resp);
    mjrpc_destroy_handle(handle);
}

void test_multi_method(void) {
    mjrpc_handle_t* handle = mjrpc_create_handle(16);
    mjrpc_add_method(handle, add_func, "add", NULL);
    mjrpc_add_method(handle, mul_func, "mul", NULL);
    // 调用 add
    const char* req1 = "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":[7,8],\"id\":10}";
    int code = -1;
    char* resp1 = mjrpc_process_str(handle, req1, &code);
    TEST_ASSERT_NOT_NULL(resp1);
    cJSON* resp_json1 = cJSON_Parse(resp1);
    TEST_ASSERT_NOT_NULL(resp_json1);
    cJSON* result1 = cJSON_GetObjectItem(resp_json1, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result1));
    TEST_ASSERT_EQUAL_INT(15, result1->valueint);
    cJSON_Delete(resp_json1);
    free(resp1);
    // 调用 mul
    const char* req2 = "{\"jsonrpc\":\"2.0\",\"method\":\"mul\",\"params\":[7,8],\"id\":11}";
    char* resp2 = mjrpc_process_str(handle, req2, &code);
    TEST_ASSERT_NOT_NULL(resp2);
    cJSON* resp_json2 = cJSON_Parse(resp2);
    TEST_ASSERT_NOT_NULL(resp_json2);
    cJSON* result2 = cJSON_GetObjectItem(resp_json2, "result");
    TEST_ASSERT_TRUE(cJSON_IsNumber(result2));
    TEST_ASSERT_EQUAL_INT(56, result2->valueint);
    cJSON_Delete(resp_json2);
    free(resp2);
    mjrpc_destroy_handle(handle);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mjrpc_add_and_call);
    RUN_TEST(test_method_not_found);
    RUN_TEST(test_invalid_params);
    RUN_TEST(test_invalid_request);
    RUN_TEST(test_batch_request);
    RUN_TEST(test_multi_method);
    return UNITY_END();
}
