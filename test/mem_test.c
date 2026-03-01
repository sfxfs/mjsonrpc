#include "unity.h"
#include "mjsonrpc.h"

#include <stdlib.h>
#include <string.h>

/* Memory tracking for hook tests */
static uint32_t custom_malloc_count = 0;
static uint32_t custom_free_count = 0;
static uint32_t custom_strdup_count = 0;

/* Custom memory functions for testing hooks */
static void* test_malloc(size_t size)
{
    custom_malloc_count++;
    return malloc(size);
}

static void test_free(void* ptr)
{
    if (ptr != NULL)
    {
        custom_free_count++;
    }
    free(ptr);
}

static char* test_strdup(const char* str)
{
    if (str == NULL)
    {
        return NULL;
    }
    custom_strdup_count++;
    size_t len = strlen(str) + 1;
    char* dup = test_malloc(len);
    if (dup != NULL)
    {
        memcpy(dup, str, len);
    }
    return dup;
}

void setUp(void)
{
    /* Reset memory counters before each test */
    custom_malloc_count = 0;
    custom_free_count = 0;
    custom_strdup_count = 0;
    /* Reset to default memory functions */
    mjrpc_set_memory_hooks(NULL, NULL, NULL);
}

void tearDown(void)
{
    /* Ensure we reset to default memory functions after each test */
    mjrpc_set_memory_hooks(NULL, NULL, NULL);
}

/* Dummy method for testing automatic resize */
static cJSON* dummy_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    return cJSON_CreateString("ok");
}

/* Method for testing memory hooks */
static cJSON* memory_test_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void) ctx;
    (void) id;

    /* Create a response with string, which triggers memory allocation */
    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "message", "memory test successful");
    cJSON_AddNumberToObject(result, "malloc_count", custom_malloc_count);
    cJSON_AddNumberToObject(result, "free_count", custom_free_count);
    cJSON_AddNumberToObject(result, "strdup_count", custom_strdup_count);

    return result;
}

void test_auto_resize(void)
{
    size_t initial_capacity = 4;
    mjrpc_handle_t* h = mjrpc_create_handle(initial_capacity);
    /* Register more methods than initial capacity, triggering resize */
    int method_count = 20;
    char name[32];
    for (int i = 0; i < method_count; ++i)
    {
        snprintf(name, sizeof(name), "m%d", i);
        int ret = mjrpc_add_method(h, dummy_func, name, NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }
    /* Verify all methods can be called correctly */
    for (int i = 0; i < method_count; ++i)
    {
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

void test_memory_hooks_set_and_reset(void)
{
    /* Test setting custom memory hooks */
    int ret = mjrpc_set_memory_hooks(test_malloc, test_free, test_strdup);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Test resetting to default memory functions */
    ret = mjrpc_set_memory_hooks(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
}

void test_memory_hooks_invalid_params(void)
{
    /* Test invalid parameter combination - setting partial functions should fail */
    int ret = mjrpc_set_memory_hooks(test_malloc, NULL, NULL);
    TEST_ASSERT_NOT_EQUAL(MJRPC_RET_OK, ret);

    ret = mjrpc_set_memory_hooks(NULL, test_free, NULL);
    TEST_ASSERT_NOT_EQUAL(MJRPC_RET_OK, ret);

    ret = mjrpc_set_memory_hooks(NULL, NULL, test_strdup);
    TEST_ASSERT_NOT_EQUAL(MJRPC_RET_OK, ret);

    ret = mjrpc_set_memory_hooks(test_malloc, test_free, NULL);
    TEST_ASSERT_NOT_EQUAL(MJRPC_RET_OK, ret);
}

void test_memory_hooks_functionality(void)
{
    /* Set custom memory hooks */
    int ret = mjrpc_set_memory_hooks(test_malloc, test_free, test_strdup);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Create handle - this should trigger memory allocation */
    size_t malloc_before = custom_malloc_count;
    size_t strdup_before = custom_strdup_count;

    mjrpc_handle_t* h = mjrpc_create_handle(4);
    TEST_ASSERT_NOT_NULL(h);

    /* Verify custom memory functions were used when creating handle */
    TEST_ASSERT_GREATER_THAN(malloc_before, custom_malloc_count);

    /* Add method - this should trigger strdup */
    ret = mjrpc_add_method(h, memory_test_func, "memory_test", NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    TEST_ASSERT_GREATER_THAN(strdup_before, custom_strdup_count);

    /* Process request - verify hooks work correctly */
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("memory_test", NULL, id);
    TEST_ASSERT_NOT_NULL(req);

    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

    /* Verify response content */
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(cJSON_IsObject(result));

    cJSON* message = cJSON_GetObjectItem(result, "message");
    TEST_ASSERT_TRUE(cJSON_IsString(message));
    TEST_ASSERT_EQUAL_STRING("memory test successful", message->valuestring);

    cJSON_Delete(resp);

    /* Record free count before destroy */
    size_t free_before = custom_free_count;

    /* Destroy handle - this should trigger memory deallocation */
    mjrpc_destroy_handle(h);

    /* Verify custom free function was used during destroy */
    TEST_ASSERT_GREATER_THAN(free_before, custom_free_count);
}

void test_memory_hooks_multiple_operations(void)
{
    /* Set custom memory hooks */
    int ret = mjrpc_set_memory_hooks(test_malloc, test_free, test_strdup);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Reset counters */
    custom_malloc_count = 0;
    custom_free_count = 0;
    custom_strdup_count = 0;

    mjrpc_handle_t* h = mjrpc_create_handle(2);
    TEST_ASSERT_NOT_NULL(h);

    /* Record strdup count before adding methods */
    size_t strdup_before = custom_strdup_count;

    /* Add multiple methods to test strdup usage */
    char method_names[3][20] = {"test1", "test2", "test3"};
    for (int i = 0; i < 3; i++)
    {
        ret = mjrpc_add_method(h, dummy_func, method_names[i], NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    /* Verify at least 3 strdup calls (may have extra due to internal implementation) */
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(3, custom_strdup_count - strdup_before);

    /* Process multiple requests */
    for (int i = 0; i < 3; i++)
    {
        cJSON* id = cJSON_CreateNumber(i);
        cJSON* req = mjrpc_request_cjson(method_names[i], NULL, id);

        int code = -1;
        cJSON* resp = mjrpc_process_cjson(h, req, &code);
        TEST_ASSERT_NOT_NULL(resp);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

        cJSON_Delete(resp);
    }

    /* Verify malloc and free were called */
    TEST_ASSERT_GREATER_THAN_size_t(0, custom_malloc_count);

    mjrpc_destroy_handle(h);

    /* Verify free was called for cleanup */
    TEST_ASSERT_GREATER_THAN_size_t(0, custom_free_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_auto_resize);
    RUN_TEST(test_memory_hooks_set_and_reset);
    RUN_TEST(test_memory_hooks_invalid_params);
    RUN_TEST(test_memory_hooks_functionality);
    RUN_TEST(test_memory_hooks_multiple_operations);
    return UNITY_END();
}
