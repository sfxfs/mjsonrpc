/**
 * @file boundary_test.c
 * @brief Boundary condition tests for edge cases
 *
 * Covers:
 *   - Empty string method names
 *   - Very long method names
 *   - Special character method names
 *   - Maximum length method names
 *   - Unicode method names
 */

#include "unity.h"
#include "mjsonrpc.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Unity setUp / tearDown                                            */
/* ------------------------------------------------------------------ */

void setUp(void) {}
void tearDown(void) {}

/* ================================================================== */
/*  Helper callbacks                                                  */
/* ================================================================== */

static cJSON* echo_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void) ctx;
    (void) params;
    return cJSON_CreateString("echo");
}

static cJSON* sum_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void) ctx;
    (void) id;
    int a = 0, b = 0;
    cJSON* a_item = cJSON_GetObjectItem(params, "a");
    cJSON* b_item = cJSON_GetObjectItem(params, "b");
    if (cJSON_IsNumber(a_item))
        a = a_item->valueint;
    if (cJSON_IsNumber(b_item))
        b = b_item->valueint;
    return cJSON_CreateNumber(a + b);
}

/* ================================================================== */
/*  Empty string method name tests                                    */
/* ================================================================== */

void test_empty_method_name_add(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* Empty string should be accepted as a valid method name */
    int ret = mjrpc_add_method(h, echo_func, "", NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Should be able to call it */
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson("", NULL, id);
    TEST_ASSERT_NOT_NULL(req);

    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

    cJSON_Delete(resp);
    mjrpc_destroy_handle(h);
}

void test_empty_method_name_del(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    mjrpc_add_method(h, echo_func, "", NULL);

    int ret = mjrpc_del_method(h, "");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Deleting again should return NOT_FOUND */
    ret = mjrpc_del_method(h, "");
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_NOT_FOUND, ret);

    mjrpc_destroy_handle(h);
}

void test_null_method_name_add(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* NULL method name should return INVALID_PARAM */
    int ret = mjrpc_add_method(h, echo_func, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_INVALID_PARAM, ret);

    mjrpc_destroy_handle(h);
}

void test_null_method_name_del(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* NULL method name should return INVALID_PARAM */
    int ret = mjrpc_del_method(h, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_ERROR_INVALID_PARAM, ret);

    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  Long method name tests                                            */
/* ================================================================== */

void test_very_long_method_name(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* Create a 1024 character method name */
    char long_name[1025];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    int ret = mjrpc_add_method(h, echo_func, long_name, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Call with long method name */
    cJSON* id = cJSON_CreateNumber(1);
    cJSON* req = mjrpc_request_cjson(long_name, NULL, id);
    TEST_ASSERT_NOT_NULL(req);

    int code = -1;
    cJSON* resp = mjrpc_process_cjson(h, req, &code);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

    cJSON_Delete(resp);

    /* Delete long method name */
    ret = mjrpc_del_method(h, long_name);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    mjrpc_destroy_handle(h);
}

void test_extremely_long_method_name(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* Create a 4096 character method name */
    char* extremely_long_name = malloc(4097);
    TEST_ASSERT_NOT_NULL(extremely_long_name);
    memset(extremely_long_name, 'b', 4096);
    extremely_long_name[4096] = '\0';

    int ret = mjrpc_add_method(h, echo_func, extremely_long_name, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Delete it */
    ret = mjrpc_del_method(h, extremely_long_name);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    free(extremely_long_name);
    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  Special character method name tests                               */
/* ================================================================== */

void test_special_characters_in_method_name(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* Test various special characters */
    const char* special_names[] = {
        "method.with.dots",
        "method-with-dashes",
        "method_with_underscores",
        "method:with:colons",
        "method/with/slashes",
        "method\\with\\backslashes",
        "method with spaces",
        "method\twith\ttabs",
        "method123with456numbers",
        "MethodWithCamelCase",
        "method_with_PascalCase",
        "method_with_snake_case",
        "method-with-kebab-case",
        "$method$with$dollar$signs",
        "method@with@at@signs",
        "method#with#hashes",
        "method%with%percent",
        "method&with&ampersands",
        "method*with*asterisks",
        "method+with+plus",
        "method=with=equals",
        "method?with?question",
        "method!with!exclaim",
    };

    int num_tests = sizeof(special_names) / sizeof(special_names[0]);

    for (int i = 0; i < num_tests; i++)
    {
        int ret = mjrpc_add_method(h, sum_func, special_names[i], NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    /* Test calling each method */
    for (int i = 0; i < num_tests; i++)
    {
        cJSON* params = cJSON_CreateObject();
        cJSON_AddNumberToObject(params, "a", 3);
        cJSON_AddNumberToObject(params, "b", 4);
        cJSON* id = cJSON_CreateNumber(i);
        cJSON* req = mjrpc_request_cjson(special_names[i], params, id);
        TEST_ASSERT_NOT_NULL(req);

        int code = -1;
        cJSON* resp = mjrpc_process_cjson(h, req, &code);
        TEST_ASSERT_NOT_NULL(resp);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

        cJSON* result = cJSON_GetObjectItem(resp, "result");
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_INT(7, result->valueint);

        cJSON_Delete(resp);
    }

    /* Delete all methods */
    for (int i = 0; i < num_tests; i++)
    {
        int ret = mjrpc_del_method(h, special_names[i]);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    mjrpc_destroy_handle(h);
}

void test_unicode_method_name(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* UTF-8 encoded method names */
    const char* unicode_names[] = {
        "\xE6\xB5\x8B\xE8\xAF\x95",  /* Chinese: 测试 (test) */
        "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82",  /* Russian: Тест (test) */
        "\xCE\x94\xCE\xBF\xCE\xBA\xCE\xB9\xCE\xBC\xCE\xAE",  /* Greek: Δοκιμή (test) */
        "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88",  /* Japanese: テスト (test) */
        "\xF0\x9F\x98\x80_method",  /* Emoji: 😀_method */
    };

    int num_tests = sizeof(unicode_names) / sizeof(unicode_names[0]);

    for (int i = 0; i < num_tests; i++)
    {
        int ret = mjrpc_add_method(h, echo_func, unicode_names[i], NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    /* Test calling each method */
    for (int i = 0; i < num_tests; i++)
    {
        cJSON* id = cJSON_CreateNumber(i);
        cJSON* req = mjrpc_request_cjson(unicode_names[i], NULL, id);
        TEST_ASSERT_NOT_NULL(req);

        int code = -1;
        cJSON* resp = mjrpc_process_cjson(h, req, &code);
        TEST_ASSERT_NOT_NULL(resp);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

        cJSON_Delete(resp);
    }

    /* Delete all methods */
    for (int i = 0; i < num_tests; i++)
    {
        int ret = mjrpc_del_method(h, unicode_names[i]);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  Hash collision tests with special names                           */
/* ================================================================== */

void test_hash_collisions_with_similar_names(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(4);  /* Small capacity to force collisions */
    TEST_ASSERT_NOT_NULL(h);

    /* Names that might hash to similar values */
    const char* names[] = {
        "ab",
        "ba",
        "aab",
        "abb",
        "abc",
        "bca",
        "cab",
    };

    int num_tests = sizeof(names) / sizeof(names[0]);

    for (int i = 0; i < num_tests; i++)
    {
        int ret = mjrpc_add_method(h, echo_func, names[i], NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    /* Verify all methods are accessible */
    for (int i = 0; i < num_tests; i++)
    {
        cJSON* id = cJSON_CreateNumber(i);
        cJSON* req = mjrpc_request_cjson(names[i], NULL, id);
        TEST_ASSERT_NOT_NULL(req);

        int code = -1;
        cJSON* resp = mjrpc_process_cjson(h, req, &code);
        TEST_ASSERT_NOT_NULL(resp);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, code);

        cJSON_Delete(resp);
    }

    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  Control character method name tests                               */
/* ================================================================== */

void test_control_characters_in_method_name(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    /* Method names with control characters */
    char ctrl_name1[] = "method\x00with\x00null";  /* Contains null bytes */
    char ctrl_name2[] = "method\nwith\nnewline";
    char ctrl_name3[] = "method\rwith\rreturn";

    /* Null-terminated string with embedded null - only first part is used */
    int ret = mjrpc_add_method(h, echo_func, ctrl_name1, NULL);
    /* This should only register "method" due to null terminator */
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    ret = mjrpc_add_method(h, echo_func, ctrl_name2, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    ret = mjrpc_add_method(h, echo_func, ctrl_name3, NULL);
    TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);

    /* Clean up */
    mjrpc_del_method(h, ctrl_name2);
    mjrpc_del_method(h, ctrl_name3);
    mjrpc_del_method(h, "method");  /* The actual registered name */

    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  Method name length boundary tests                                 */
/* ================================================================== */

void test_single_char_method_name(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    TEST_ASSERT_NOT_NULL(h);

    const char* single_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

    size_t num_tests = strlen(single_chars);

    for (size_t i = 0; i < num_tests; i++)
    {
        char name[2] = {single_chars[i], '\0'};
        int ret = mjrpc_add_method(h, echo_func, name, NULL);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    /* Delete all */
    for (size_t i = 0; i < num_tests; i++)
    {
        char name[2] = {single_chars[i], '\0'};
        int ret = mjrpc_del_method(h, name);
        TEST_ASSERT_EQUAL_INT(MJRPC_RET_OK, ret);
    }

    mjrpc_destroy_handle(h);
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Empty string tests */
    RUN_TEST(test_empty_method_name_add);
    RUN_TEST(test_empty_method_name_del);
    RUN_TEST(test_null_method_name_add);
    RUN_TEST(test_null_method_name_del);

    /* Long method name tests */
    RUN_TEST(test_very_long_method_name);
    RUN_TEST(test_extremely_long_method_name);

    /* Special character tests */
    RUN_TEST(test_special_characters_in_method_name);
    RUN_TEST(test_unicode_method_name);

    /* Hash collision tests */
    RUN_TEST(test_hash_collisions_with_similar_names);

    /* Control character tests */
    RUN_TEST(test_control_characters_in_method_name);

    /* Single character tests */
    RUN_TEST(test_single_char_method_name);

    return UNITY_END();
}
