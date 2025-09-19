#include "unity.h"
#include "mjsonrpc.h"

#include <string.h>
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

void test_client_build_notif_request_cjson(void)
{
    cJSON* ret = mjrpc_request_cjson("notif", NULL, NULL);
    TEST_ASSERT_NOT_NULL(ret);
    char* ret_str = cJSON_PrintUnformatted(ret);
    TEST_ASSERT_NOT_NULL(ret_str);
    TEST_ASSERT_EQUAL_STRING("{\"jsonrpc\":\"2.0\",\"method\":\"notif\"}", ret_str);
    free(ret_str);
    cJSON_Delete(ret);
}

void test_client_build_notif_request_str(void)
{
    char* ret = mjrpc_request_str("notif", NULL, NULL);
    TEST_ASSERT_NOT_NULL(ret);
    TEST_ASSERT_EQUAL_STRING("{\"jsonrpc\":\"2.0\",\"method\":\"notif\"}", ret);
    free(ret);
}

void test_client_build_null_id_request_cjson(void)
{
    cJSON* id = cJSON_CreateNull();
    cJSON* ret = mjrpc_request_cjson("null_id", NULL, id);
    TEST_ASSERT_NOT_NULL(ret);
    char* ret_str = cJSON_PrintUnformatted(ret);
    TEST_ASSERT_NOT_NULL(ret_str);
    TEST_ASSERT_EQUAL_STRING("{\"jsonrpc\":\"2.0\",\"method\":\"null_id\",\"id\":null}", ret_str);
    free(ret_str);
    cJSON_Delete(ret);
}

void test_client_build_null_id_request_str(void)
{
    cJSON* id = cJSON_CreateNull();
    char* ret = mjrpc_request_str("null_id", NULL, id);
    TEST_ASSERT_NOT_NULL(ret);
    TEST_ASSERT_EQUAL_STRING("{\"jsonrpc\":\"2.0\",\"method\":\"null_id\",\"id\":null}", ret);
    free(ret);
}

void test_client_build_with_params_request_cjson(void)
{
    cJSON* params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateString("param1"));
    cJSON_AddItemToArray(params, cJSON_CreateNumber(42));
    cJSON* id = cJSON_CreateString("req-1");
    cJSON* ret = mjrpc_request_cjson("method_with_params", params, id);
    TEST_ASSERT_NOT_NULL(ret);
    char* ret_str = cJSON_PrintUnformatted(ret);
    TEST_ASSERT_NOT_NULL(ret_str);
    TEST_ASSERT_EQUAL_STRING("{\"jsonrpc\":\"2.0\",\"method\":\"method_with_params\",\"params\":["
                             "\"param1\",42],\"id\":\"req-1\"}",
                             ret_str);
    free(ret_str);
    cJSON_Delete(ret);
}

void test_client_build_with_params_request_str(void)
{
    cJSON* params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateString("param1"));
    cJSON_AddItemToArray(params, cJSON_CreateNumber(42));
    cJSON* id = cJSON_CreateString("req-1");
    char* ret = mjrpc_request_str("method_with_params", params, id);
    TEST_ASSERT_NOT_NULL(ret);
    TEST_ASSERT_EQUAL_STRING("{\"jsonrpc\":\"2.0\",\"method\":\"method_with_params\",\"params\":["
                             "\"param1\",42],\"id\":\"req-1\"}",
                             ret);
    free(ret);
}

void test_client_build_no_method_name_request_cjson(void)
{
    cJSON* ret = mjrpc_request_cjson(NULL, NULL, NULL);
    TEST_ASSERT_NULL(ret);
    ret = mjrpc_request_cjson(NULL, NULL, cJSON_CreateNull());
    TEST_ASSERT_NULL(ret);
    ret = mjrpc_request_cjson(NULL, cJSON_CreateNumber(1), cJSON_CreateNull());
    TEST_ASSERT_NULL(ret);
}

void test_client_build_no_method_name_request_str(void)
{
    char* ret = mjrpc_request_str(NULL, NULL, NULL);
    TEST_ASSERT_NULL(ret);
    ret = mjrpc_request_str(NULL, NULL, cJSON_CreateNull());
    TEST_ASSERT_NULL(ret);
    ret = mjrpc_request_str(NULL, cJSON_CreateNumber(1), cJSON_CreateNull());
    TEST_ASSERT_NULL(ret);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_client_build_notif_request_cjson);
    RUN_TEST(test_client_build_notif_request_str);
    RUN_TEST(test_client_build_null_id_request_cjson);
    RUN_TEST(test_client_build_null_id_request_str);
    RUN_TEST(test_client_build_with_params_request_cjson);
    RUN_TEST(test_client_build_with_params_request_str);
    RUN_TEST(test_client_build_no_method_name_request_cjson);
    RUN_TEST(test_client_build_no_method_name_request_str);
    return UNITY_END();
}
