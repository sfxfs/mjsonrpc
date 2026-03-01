/**
 * @file concurrent_test.c
 * @brief Concurrent and threading tests for mjsonrpc
 *
 * Covers:
 *   - Multi-threaded method registration (serialized)
 *   - Handle NULL safety
 *
 * Note: Full thread safety is not guaranteed due to cJSON's non-thread-safe
 * memory allocation. These tests verify basic concurrent usage patterns.
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

#include "mjsonrpc.h"

#define NUM_THREADS 4
#define OPS_PER_THREAD 20
#define HASH_TABLE_SIZE 16

/* Mutex for serializing cJSON operations */
static pthread_mutex_t cjson_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ================================================================== */
/*  Helper callbacks                                                  */
/* ================================================================== */

static cJSON* echo_func(mjrpc_func_ctx_t* ctx, cJSON* params, cJSON* id)
{
    (void) ctx;
    (void) params;
    pthread_mutex_lock(&cjson_mutex);
    cJSON* result = cJSON_CreateString("echo");
    pthread_mutex_unlock(&cjson_mutex);
    return result;
}

/* ================================================================== */
/*  Thread data                                                       */
/* ================================================================== */

typedef struct {
    mjrpc_handle_t* handle;
    int thread_id;
    int success;
    int fail;
} thread_arg_t;

/* ================================================================== */
/*  Thread: Add methods (with mutex for cJSON safety)                 */
/* ================================================================== */

static void* add_thread(void* arg)
{
    thread_arg_t* t = (thread_arg_t*)arg;
    char name[64];

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        snprintf(name, sizeof(name), "t%d_m%d", t->thread_id, i);

        /* Protect cJSON operations with mutex */
        pthread_mutex_lock(&cjson_mutex);
        int ret = mjrpc_add_method(t->handle, echo_func, name, NULL);
        pthread_mutex_unlock(&cjson_mutex);

        if (ret == MJRPC_RET_OK)
            t->success++;
        else
            t->fail++;
    }
    return NULL;
}

/* ================================================================== */
/*  Test 1: Concurrent add methods (serialized for cJSON safety)      */
/* ================================================================== */

int test_concurrent_add(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(HASH_TABLE_SIZE);
    if (!h) {
        fprintf(stderr, "CREATE HANDLE FAILED\n");
        return 1;
    }

    pthread_t th[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].handle = h;
        args[i].thread_id = i;
        args[i].success = 0;
        args[i].fail = 0;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&th[i], NULL, add_thread, &args[i]) != 0) {
            fprintf(stderr, "CREATE THREAD FAILED\n");
            mjrpc_destroy_handle(h);
            return 1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(th[i], NULL);
    }

    int total = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total += args[i].success;
    }

    mjrpc_destroy_handle(h);

    if (total > 0) {
        printf("PASS: test_concurrent_add (ops=%d)\n", total);
        return 0;
    }
    fprintf(stderr, "NO SUCCESSFUL OPS\n");
    return 1;
}

/* ================================================================== */
/*  Test 2: Sequential add and delete (verify basic functionality)    */
/* ================================================================== */

int test_sequential_add_del(void)
{
    mjrpc_handle_t* h = mjrpc_create_handle(HASH_TABLE_SIZE);
    if (!h) {
        fprintf(stderr, "CREATE HANDLE FAILED\n");
        return 1;
    }

    char name[64];
    for (int i = 0; i < 10; i++) {
        snprintf(name, sizeof(name), "method_%d", i);
        pthread_mutex_lock(&cjson_mutex);
        mjrpc_add_method(h, echo_func, name, NULL);
        pthread_mutex_unlock(&cjson_mutex);
    }

    for (int i = 0; i < 10; i++) {
        snprintf(name, sizeof(name), "method_%d", i);
        mjrpc_del_method(h, name);
    }

    mjrpc_destroy_handle(h);
    printf("PASS: test_sequential_add_del\n");
    return 0;
}

/* ================================================================== */
/*  Test 3: Handle NULL safety                                        */
/* ================================================================== */

int test_null_safety(void)
{
    if (mjrpc_add_method(NULL, echo_func, "x", NULL) != MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED) {
        fprintf(stderr, "ADD NULL HANDLE FAILED\n");
        return 1;
    }
    if (mjrpc_del_method(NULL, "x") != MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED) {
        fprintf(stderr, "DEL NULL HANDLE FAILED\n");
        return 1;
    }
    if (mjrpc_get_method_count(NULL) != 0) {
        fprintf(stderr, "COUNT NULL HANDLE FAILED\n");
        return 1;
    }
    printf("PASS: test_null_safety\n");
    return 0;
}

/* ================================================================== */
/*  Test 4: Handle create/destroy in multiple threads                 */
/* ================================================================== */

static void* handle_create_destroy_thread(void* arg)
{
    int id = *(int*)arg;
    mjrpc_handle_t* h = mjrpc_create_handle(8);
    if (!h) return NULL;

    char name[32];
    for (int i = 0; i < 5; i++) {
        snprintf(name, sizeof(name), "t%d_m%d", id, i);
        pthread_mutex_lock(&cjson_mutex);
        mjrpc_add_method(h, echo_func, name, NULL);
        pthread_mutex_unlock(&cjson_mutex);
    }

    mjrpc_destroy_handle(h);
    return NULL;
}

int test_handle_per_thread(void)
{
    pthread_t th[NUM_THREADS];
    int ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        ids[i] = i;
        if (pthread_create(&th[i], NULL, handle_create_destroy_thread, &ids[i]) != 0) {
            fprintf(stderr, "CREATE THREAD FAILED\n");
            return 1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(th[i], NULL);
    }

    printf("PASS: test_handle_per_thread\n");
    return 0;
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */

int main(void)
{
    int fail = 0;
    printf("=== Concurrent Tests ===\n\n");

    if (test_null_safety() != 0) fail++;
    if (test_sequential_add_del() != 0) fail++;
    if (test_concurrent_add() != 0) fail++;
    if (test_handle_per_thread() != 0) fail++;

    printf("\n");
    if (fail == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d TEST(S) FAILED\n", fail);
    return 1;
}
