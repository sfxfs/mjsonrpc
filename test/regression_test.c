/**
 * @file regression_test.c
 * @brief Regression tests for bugs fixed during code review
 *
 * Covers:
 *   BUG-3  : Batch all-notifications should not leak return_json_array
 *   BUG-4  : Callback sets error_code AND returns cJSON (return value must be freed)
 *   BUG-8  : Corrected JSON-RPC error codes (-32602, -32603)
 *   OPT-5  : mjrpc_del_method(NULL, ...) returns HANDLE_NOT_INITIALIZED
 *   HASH-1 : Probe loop bounded when table is nearly full
 */

#include "unity.h"
#include "mjsonrpc.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Memory-tracking allocator for leak detection                      */
/* ------------------------------------------------------------------ */

static int alloc_balance = 0; /* +1 on malloc/strdup, -1 on free */

static void* tracking_malloc(size_t size)
{
    void* p = malloc(size);
    if (p)
        alloc_balance++;
    return p;
}

static void tracking_free(void* ptr)
{
    if (ptr)
        alloc_balance--;
    free(ptr);
}

static char* tracking_strdup(const char* str)
{
    if (!str)
        return NULL;
    size_t len = strlen(str) + 1;
    char* dup = tracking_malloc(len);
    if (dup)
        memcpy(dup, str, len);
    return dup;
}

/* ------------------------------------------------------------------ */
/*  Unity setUp / tearDown                                            */
/* ------------------------------------------------------------------ */

void setUp(void)
{
    /* Reset to default allocator before every test */
    mjrpc_set_memory_hooks(NULL, NULL, NULL);
    alloc_balance = 0;
}

void tearDown(void)
{
    mjrpc_set_memory_hooks(NULL, NULL, NULL);
}

/* ================================================================== */
/*  Helper callbacks                                                  */
/* ================================================================== */

/* A simple notification handler (returns NULL, no error) */
static cJSON* notif_handler(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void)ctx;
    (void)params;
    (void)id;
    return NULL; /* notification: no result */
}

/* BUG-4 callback: sets error_code BUT also returns a non-NULL cJSON.
 * Before the fix, the returned cJSON would be leaked. */
static cJSON* error_and_return_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void)params;
    (void)id;
    ctx->error_code = -32001;
    ctx->error_message = strdup("callback error");
    /* Deliberately return a cJSON object that should be freed by the library */
    return cJSON_CreateString("this should be freed");
}

/* A trivial OK handler */
static cJSON* ok_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void)ctx;
    (void)params;
    (void)id;
    return cJSON_CreateString("ok");
}

/* ================================================================== */
/*  BUG-3 : Batch request where ALL items are notifications           */
/*          Before fix, return_json_array was leaked.                  */
/* ================================================================== */

void test_bug3_batch_all_notifications(void)
{
    mjrpc_set_memory_hooks(tracking_malloc, tracking_free, tracking_strdup);
    alloc_balance = 0;

    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);
    mjrpc_add_method(h, notif_handler, "notif", NULL);

    /* Build a batch of 3 notification requests (no "id" field) */
    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < 3; i++)
    {
        cJSON* req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "jsonrpc", "2.0");
        cJSON_AddStringToObject(req, "method", "notif");
        cJSON_AddItemToArray(arr, req);
    }

    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, arr, &code);

    /* All notifications -> response must be NULL */
    TEST_ASSERT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK_NOTIFICATION, code);

    cJSON_Delete(arr);
    mjrpc_destroy_handle(h);

    /* After destroying everything, alloc_balance should be 0 (no leak) */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, alloc_balance,
                                  "BUG-3: memory leak detected in batch all-notifications path");
}

/* ================================================================== */
/*  BUG-4 : Callback sets error_code AND returns non-NULL cJSON       */
/*          Before fix, the returned cJSON was leaked.                */
/* ================================================================== */

void test_bug4_callback_error_with_return(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);
    mjrpc_add_method(h, error_and_return_func, "err_ret", NULL);

    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("err_ret", NULL, id);
    TEST_ASSERT_NOT_NULL(req);

    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);

    /* Response must be an error, not a success result */
    cJSON* error = cJSON_GetObjectItem(resp, "error");
    TEST_ASSERT_NOT_NULL(error);

    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_EQUAL_INT(-32001, code_item->valueint);

    cJSON* msg_item = cJSON_GetObjectItem(error, "message");
    TEST_ASSERT_EQUAL_STRING("callback error", msg_item->valuestring);

    /* There should be no "result" field */
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_NULL(result);

    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

/* BUG-4 callback that uses tracking_strdup for error_message,
 * so the tracking allocator stays balanced. */
static cJSON* error_and_return_func_tracked(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void)params;
    (void)id;
    ctx->error_code = -32001;
    /* Use tracking_strdup so the allocation is balanced with tracking_free */
    ctx->error_message = tracking_strdup("callback error");
    return cJSON_CreateString("this should be freed");
}

/* Test with tracking allocator to ensure the returned cJSON is freed */
void test_bug4_callback_error_no_leak(void)
{
    mjrpc_set_memory_hooks(tracking_malloc, tracking_free, tracking_strdup);
    alloc_balance = 0;

    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);
    mjrpc_add_method(h, error_and_return_func_tracked, "err_ret", NULL);

    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("err_ret", NULL, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);

    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, alloc_balance,
                                  "BUG-4: memory leak when callback sets error_code and returns cJSON");
}

/* ================================================================== */
/*  BUG-8 : Verify corrected JSON-RPC error code values               */
/*          INVALID_PARAMS was -32603 (wrong), should be -32602       */
/*          INTERNAL_ERROR was -32693 (wrong), should be -32603       */
/* ================================================================== */

void test_bug8_error_code_values(void)
{
    /* JSON-RPC 2.0 spec defines these exact values */
    TEST_ASSERT_EQUAL_INT(-32700, JSON_RPC_CODE_PARSE_ERROR);
    TEST_ASSERT_EQUAL_INT(-32600, JSON_RPC_CODE_INVALID_REQUEST);
    TEST_ASSERT_EQUAL_INT(-32601, JSON_RPC_CODE_METHOD_NOT_FOUND);
    TEST_ASSERT_EQUAL_INT(-32602, JSON_RPC_CODE_INVALID_PARAMS);
    TEST_ASSERT_EQUAL_INT(-32603, JSON_RPC_CODE_INTERNAL_ERROR);
}

/* Verify error response carries the correct code when method not found */
void test_bug8_method_not_found_code_in_response(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    /* Don't register any methods */

    cJSON* id = cJSON_CreateNumber(99);
    cJSON* req = mjrpc_request_cjson("nonexistent", NULL, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);

    cJSON* error = cJSON_GetObjectItem(resp, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_EQUAL_INT(-32601, code_item->valueint);

    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  OPT-5 : mjrpc_del_method(NULL, "name") guard                     */
/*          Before fix, NULL handle was not checked in del_method.    */
/* ================================================================== */

void test_opt5_del_method_null_handle(void)
{
    int ret = mjrpc_del_method(NULL, "anything");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED, ret);
}

void test_opt5_del_method_null_name(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    int ret = mjrpc_del_method(h, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_INVALID_PARAM, ret);
    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  HASH-1 : Probe loop bounded - searching for a method that does   */
/*           not exist in a heavily loaded table must terminate.      */
/* ================================================================== */

void test_hash1_probe_loop_terminates(void)
{
    /* Use a very small capacity to force collisions and high load */
    size_t cap = 4;
    mjrpc_handle_t* h = mjrpc_create_handle(cap);
    TEST_ASSERT_NOT_NULL(h);

    /* Fill the table with enough methods to trigger resize(s) and
     * keep the load factor high. We register unique names. */
    char name[32];
    int count = 30; /* well above initial capacity */
    for (int i = 0; i < count; i++)
    {
        snprintf(name, sizeof(name), "method_%d", i);
        int ret = mjrpc_add_method(h, ok_func, name, NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    /* Lookup a method that does NOT exist.
     * Before HASH-1 fix, this could loop forever on a full table. */
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("no_such_method", NULL, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);

    cJSON* error = cJSON_GetObjectItem(resp, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON* code_item = cJSON_GetObjectItem(error, "code");
    TEST_ASSERT_EQUAL_INT(JSON_RPC_CODE_METHOD_NOT_FOUND, code_item->valueint);

    cJSON_Delete(resp);

    /* Also test del_method on a non-existent method in a loaded table */
    int ret = mjrpc_del_method(h, "no_such_method");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_NOT_FOUND, ret);

    mjrpc_destroy_handle(h);
}

/* Test that add/delete/re-add cycle works correctly with probe bounds */
void test_hash1_add_delete_readd(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(4);
    TEST_ASSERT_NOT_NULL(h);

    /* Add two methods */
    mjrpc_add_method(h, ok_func, "alpha", NULL);
    mjrpc_add_method(h, ok_func, "beta", NULL);

    /* Delete the first */
    int ret = mjrpc_del_method(h, "alpha");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* "beta" should still be accessible (probes past DELETED slot) */
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("beta", NULL, id);
    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("ok", result->valuestring);
    cJSON_Delete(resp);

    /* Re-add "alpha" - should reuse the DELETED slot */
    ret = mjrpc_add_method(h, ok_func, "alpha", NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Verify "alpha" works again */
    id = cJSON_CreateNumber(2);
    req = mjrpc_request_cjson("alpha", NULL, id);
    code = -1;
    resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("ok", result->valuestring);
    cJSON_Delete(resp);

    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* BUG-3 */
    RUN_TEST(test_bug3_batch_all_notifications);

    /* BUG-4 */
    RUN_TEST(test_bug4_callback_error_with_return);
    RUN_TEST(test_bug4_callback_error_no_leak);

    /* BUG-8 */
    RUN_TEST(test_bug8_error_code_values);
    RUN_TEST(test_bug8_method_not_found_code_in_response);

    /* OPT-5 */
    RUN_TEST(test_opt5_del_method_null_handle);
    RUN_TEST(test_opt5_del_method_null_name);

    /* HASH-1 */
    RUN_TEST(test_hash1_probe_loop_terminates);
    RUN_TEST(test_hash1_add_delete_readd);

    return UNITY_END();
}
