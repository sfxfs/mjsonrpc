#include "unity.h"
#include "mjsonrpc.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Test custom error code and message */
static cJSON* error_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    ctx->error_code = -32001;
    ctx->error_message = strdup("custom error");
    return NULL;
}

void test_custom_error(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, error_func, "err", NULL);
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("err", NULL, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* error = cJSON_GetObjectItem(resp, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    cJSON* msg_item = cJSON_GetObjectItem(error, "message");
    TEST_ASSERT_EQUAL_INT(-32001, code_item->valueint);
    TEST_ASSERT_EQUAL_STRING("custom error", msg_item->valuestring);
    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

/* Test no id error */
void test_no_id_error(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    mjrpc_add_method(h, error_func, "err", NULL);
    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", "err");
    /* No id */
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NULL(resp);
    mjrpc_destroy_handle(h);
}

/* Test process_str parse failure */
void test_process_str_parse_fail(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    int code = -1;
    char* resp = mjrpc_process_str(h, "not a json", &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* resp_json = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(resp_json);
    cJSON* error = cJSON_GetObjectItem(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    cJSON* msg_item = cJSON_GetObjectItem(error, "message");
    TEST_ASSERT_EQUAL_INT(JSON_RPC_CODE_PARSE_ERROR, code_item->valueint);
    TEST_ASSERT_EQUAL_STRING("Invalid request received: Not a JSON formatted request.",
                             msg_item->valuestring);
    cJSON_Delete(resp_json);
    free(resp);
    mjrpc_destroy_handle(h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_custom_error);
    RUN_TEST(test_no_id_error);
    RUN_TEST(test_process_str_parse_fail);
    return UNITY_END();
}
