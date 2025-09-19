// 针对 mjsonrpc.c 未覆盖分支的补充测试
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "mjsonrpc.h"

void setUp(void) {}
void tearDown(void) {}

// 空 handle
void test_null_handle(void) {
    int code = 0;
    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", "add");
    cJSON_AddItemToObject(req, "params", cJSON_CreateIntArray((int[]){1,2}, 2));
    cJSON_AddNumberToObject(req, "id", 1);
    cJSON* resp = mjrpc_process_cjson(NULL, req, &code);
    TEST_ASSERT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED, code);
    cJSON_Delete(req);
}

// 非法 JSON
void test_invalid_json(void) {
    int code = 0;
    cJSON* resp = mjrpc_process_cjson(mjrpc_create_handle(8), NULL, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_PARSE_FAILED, code);
    cJSON_Delete(resp);
}

// 空数组请求
void test_empty_array_req(void) {
    int code = 0;
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    cJSON* arr = cJSON_CreateArray();
    cJSON* resp = mjrpc_process_cjson(h, arr, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_EMPTY_REQUEST, code);
    cJSON_Delete(resp);
    cJSON_Delete(arr);
    mjrpc_destroy_handle(h);
}

// 空对象请求
void test_empty_obj_req(void) {
    int code = 0;
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    cJSON* obj = cJSON_CreateObject();
    cJSON* resp = mjrpc_process_cjson(h, obj, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_EMPTY_REQUEST, code);
    cJSON_Delete(resp);
    cJSON_Delete(obj);
    mjrpc_destroy_handle(h);
}

// 非对象非数组
void test_not_obj_ary(void) {
    int code = 0;
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    cJSON* str = cJSON_CreateString("not a req");
    cJSON* resp = mjrpc_process_cjson(h, str, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_NOT_OBJ_ARY, code);
    cJSON_Delete(resp);
    cJSON_Delete(str);
    mjrpc_destroy_handle(h);
}

// 错误方法名
void test_method_not_found2(void) {
    int code = 0;
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", "no_such_method");
    cJSON_AddItemToObject(req, "params", cJSON_CreateIntArray((int[]){1,2}, 2));
    cJSON_AddNumberToObject(req, "id", 1);
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON_Delete(resp);
    cJSON_Delete(req);
    mjrpc_destroy_handle(h);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null_handle);
    RUN_TEST(test_invalid_json);
    RUN_TEST(test_empty_array_req);
    RUN_TEST(test_empty_obj_req);
    RUN_TEST(test_not_obj_ary);
    RUN_TEST(test_method_not_found2);
    return UNITY_END();
}
