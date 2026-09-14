/**
 * @file benchmark.c
 * @brief Performance benchmark suite for mjsonrpc
 *
 * Measures three dimensions of performance:
 *
 *  1. Speed          - wall-clock and CPU time per operation (ns/op, ops/s),
 *                      using calibrated iteration counts, warmup runs and
 *                      multiple samples (median / min / stddev reported).
 *  2. Memory         - allocation count, allocated bytes and peak live heap
 *                      bytes per operation, captured through the memory hooks
 *                      of mjsonrpc and cJSON (cJSON is linked as a shared
 *                      library, so the hooks cover library-internal
 *                      allocations as well).
 *  3. Resource usage - process-level metrics from getrusage(): peak RSS,
 *                      user/system CPU time, page faults, context switches.
 *
 * The suite doubles as a self-check (`--check`):
 *  - every byte allocated through the hooks must be freed again (no leaks);
 *  - each benchmark must stay above a conservative absolute lower bound
 *    (catches catastrophic regressions such as accidental O(n^2) behavior).
 *
 * Usage:
 *   mjsonrpc-benchmark [--json FILE] [--markdown FILE] [--check]
 *                      [--quick] [--filter SUBSTR] [--help]
 *
 * Exit codes:
 *   0 - success
 *   1 - usage error
 *   2 - self-check failure (leak detected or lower bound violated)
 */

/* _DEFAULT_SOURCE exposes POSIX APIs on glibc; macOS headers are fine with
 * the default feature macros and must not be restricted by _POSIX_C_SOURCE. */
#define _DEFAULT_SOURCE

#include "mjsonrpc.h"
#include "cJSON.h"

#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/utsname.h>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#endif

#ifndef MJSONRPC_VERSION
#define MJSONRPC_VERSION "unknown"
#endif
#ifndef BENCH_BUILD_TYPE
#define BENCH_BUILD_TYPE "unknown"
#endif

/* ================================================================== */
/*  Global options                                                    */
/* ================================================================== */

static int g_samples = 7;
static double g_calib_target_secs = 0.030;
static int g_check_mode = 0;
static const char *g_filter = NULL;
static int g_trace = 0; /* BENCH_TRACE=1: per-phase timings to stderr */

/* ================================================================== */
/*  Memory tracking                                                   */
/*                                                                    */
/*  All allocations made by cJSON and mjsonrpc go through the hooks   */
/*  below. Each block carries a small header that stores its size so  */
/*  that frees can be accounted too. Allocation counters are atomic   */
/*  because the multi-threaded benchmark runs workers concurrently.   */
/* ================================================================== */

typedef union {
  size_t size;
  max_align_t align;
} alloc_header_t;

#define HDR_SIZE ((size_t)sizeof(alloc_header_t))

static _Atomic long long g_allocs = 0;
static _Atomic long long g_frees = 0;
static _Atomic long long g_alloc_bytes = 0;
static _Atomic long long g_free_bytes = 0;
static _Atomic long long g_live_bytes = 0;
static _Atomic long long g_peak_bytes = 0;

static void track_peak(long long live)
{
  long long peak = atomic_load_explicit(&g_peak_bytes, memory_order_relaxed);
  while (live > peak &&
         !atomic_compare_exchange_weak_explicit(&g_peak_bytes, &peak, live,
                                                memory_order_relaxed,
                                                memory_order_relaxed)) {
    /* peak refreshed by the failed CAS, retry */
  }
}

/* Reset the peak watermark before a memory measurement phase.  The
 * cumulative allocation counters (g_allocs/g_frees/...) are global and
 * intentionally never reset: they are the basis of the leak check, and
 * per-benchmark deltas are computed by the caller. */
static void track_begin_phase(void)
{
  long long live = atomic_load(&g_live_bytes);
  atomic_store(&g_peak_bytes, live);
}

static void *bench_malloc(size_t size)
{
  void *raw = malloc(size + HDR_SIZE);
  if (raw == NULL) {
    return NULL;
  }
  alloc_header_t *hdr = (alloc_header_t *)raw;
  hdr->size = size;

  long long live = atomic_fetch_add(&g_live_bytes, (long long)size) +
                   (long long)size;
  atomic_fetch_add(&g_allocs, 1);
  atomic_fetch_add(&g_alloc_bytes, (long long)size);
  track_peak(live);

  return (char *)raw + HDR_SIZE;
}

static void bench_free(void *ptr)
{
  if (ptr == NULL) {
    return;
  }
  alloc_header_t *hdr = (alloc_header_t *)((char *)ptr - HDR_SIZE);
  size_t size = hdr->size;

  atomic_fetch_sub(&g_live_bytes, (long long)size);
  atomic_fetch_add(&g_frees, 1);
  atomic_fetch_add(&g_free_bytes, (long long)size);

  free(hdr);
}

static char *bench_strdup(const char *str)
{
  if (str == NULL) {
    return NULL;
  }
  size_t len = strlen(str) + 1;
  char *copy = (char *)bench_malloc(len);
  if (copy != NULL) {
    memcpy(copy, str, len);
  }
  return copy;
}

/* ================================================================== */
/*  Timing helpers                                                    */
/* ================================================================== */

static double now_sec(clockid_t clock_id)
{
#if defined(__APPLE__)
  /* mach_absolute_time() is faster and more reliable than clock_gettime()
   * for the monotonic clock on macOS. */
  if (clock_id == CLOCK_MONOTONIC) {
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0) {
      mach_timebase_info(&timebase);
    }
    return (double)mach_absolute_time() * (double)timebase.numer /
           ((double)timebase.denom * 1e9);
  }
#endif
  struct timespec ts;
  if (clock_gettime(clock_id, &ts) == 0) {
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
  }
  /* Last-resort fallback (wall clock); never expected to run. */
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

static int cmp_double(const void *a, const void *b)
{
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

static double median_of(double *values, int count)
{
  qsort(values, (size_t)count, sizeof(double), cmp_double);
  if (count % 2 == 1) {
    return values[count / 2];
  }
  return 0.5 * (values[count / 2 - 1] + values[count / 2]);
}

static double stddev_of(const double *values, int count, double mean)
{
  if (count < 2) {
    return 0.0;
  }
  double sum = 0.0;
  for (int i = 0; i < count; i++) {
    double d = values[i] - mean;
    sum += d * d;
  }
  return sqrt(sum / (double)(count - 1));
}

/* ================================================================== */
/*  Benchmark definitions                                             */
/* ================================================================== */

typedef long long (*bench_op_fn)(void *ctx, long long iters);
typedef void (*bench_setup_fn)(void *ctx);
typedef void (*bench_teardown_fn)(void *ctx);
typedef long long (*bench_ops_for_fn)(long long iters);

typedef struct bench_def {
  const char *name;
  const char *description;
  bench_setup_fn setup;
  bench_op_fn op;
  bench_teardown_fn teardown;
  bench_ops_for_fn ops_for; /* optional, maps iters to executed op count */
  void *ctx;
  long long min_ops_per_sec; /* conservative lower bound, 0 = none */
} bench_def_t;

/* ------------------------------------------------------------------ */
/*  RPC method callbacks                                              */
/* ------------------------------------------------------------------ */

static cJSON *cb_echo(mjrpc_func_ctx_t *context, cJSON *params, cJSON *id)
{
  (void)context;
  (void)params;
  (void)id;
  return cJSON_CreateString("ok");
}

static cJSON *cb_sum(mjrpc_func_ctx_t *context, cJSON *params, cJSON *id)
{
  (void)context;
  (void)id;
  double a = 0.0;
  double b = 0.0;
  cJSON *ja = cJSON_GetObjectItemCaseSensitive(params, "a");
  cJSON *jb = cJSON_GetObjectItemCaseSensitive(params, "b");
  if (cJSON_IsNumber(ja)) {
    a = ja->valuedouble;
  }
  if (cJSON_IsNumber(jb)) {
    b = jb->valuedouble;
  }
  return cJSON_CreateNumber(a + b);
}

/* ------------------------------------------------------------------ */
/*  Handle lifecycle                                                  */
/* ------------------------------------------------------------------ */

static long long op_handle_lifecycle(void *ctx, long long iters)
{
  (void)ctx;
  for (long long i = 0; i < iters; i++) {
    mjrpc_handle_t *handle = mjrpc_create_handle(16);
    mjrpc_destroy_handle(handle);
  }
  return iters;
}

/* ------------------------------------------------------------------ */
/*  Method registration (1000 methods per batch)                      */
/* ------------------------------------------------------------------ */

#define ADD_METHOD_COUNT 1000
#define ADD_METHOD_NAME_LEN 24

typedef struct {
  char names[ADD_METHOD_COUNT][ADD_METHOD_NAME_LEN];
} add_method_ctx_t;

static add_method_ctx_t g_add_method_ctx;

static void setup_add_method(void *ctx)
{
  add_method_ctx_t *c = (add_method_ctx_t *)ctx;
  for (int i = 0; i < ADD_METHOD_COUNT; i++) {
    snprintf(c->names[i], ADD_METHOD_NAME_LEN, "bench_method_%04d", i);
  }
}

static long long op_add_method(void *ctx, long long iters)
{
  add_method_ctx_t *c = (add_method_ctx_t *)ctx;
  long long done = 0;
  for (long long i = 0; i < iters; i++) {
    mjrpc_handle_t *handle = mjrpc_create_handle(16);
    if (handle == NULL) {
      break;
    }
    for (int m = 0; m < ADD_METHOD_COUNT; m++) {
      mjrpc_add_method(handle, cb_echo, c->names[m], NULL);
    }
    mjrpc_destroy_handle(handle);
    done++;
  }
  return done;
}

/* ------------------------------------------------------------------ */
/*  Request generation (client side)                                  */
/* ------------------------------------------------------------------ */

static long long op_request_str_small(void *ctx, long long iters)
{
  (void)ctx;
  for (long long i = 0; i < iters; i++) {
    cJSON *id = cJSON_CreateNumber((double)i);
    char *request = mjrpc_request_str("bench_echo", NULL, id);
    bench_free(request);
  }
  return iters;
}

static long long op_request_str_params(void *ctx, long long iters)
{
  (void)ctx;
  for (long long i = 0; i < iters; i++) {
    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
      break;
    }
    cJSON_AddNumberToObject(params, "a", (double)i);
    cJSON_AddNumberToObject(params, "b", (double)i + 1);
    cJSON_AddStringToObject(params, "name", "benchmark");
    cJSON_AddBoolToObject(params, "flag", 1);
    cJSON_AddNullToObject(params, "empty");
    cJSON *id = cJSON_CreateNumber((double)i);
    char *request = mjrpc_request_str("bench_sum", params, id);
    bench_free(request);
  }
  return iters;
}

/* ------------------------------------------------------------------ */
/*  Request processing (server side)                                  */
/* ------------------------------------------------------------------ */

typedef struct {
  mjrpc_handle_t *handle;
  char *request; /* owned */
} process_ctx_t;

static long long op_process_str(void *ctx, long long iters)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  int ret_code = 0;
  for (long long i = 0; i < iters; i++) {
    char *response = mjrpc_process_str(c->handle, c->request, &ret_code);
    bench_free(response);
  }
  return iters;
}

/* sanity check a request/response pair once, before measuring */
static void process_sanity_check(process_ctx_t *c, int expect_ret)
{
  int ret_code = -1;
  char *response = mjrpc_process_str(c->handle, c->request, &ret_code);
  if (ret_code != expect_ret) {
    fprintf(stderr, "benchmark setup error: unexpected ret code %d (want %d)\n",
            ret_code, expect_ret);
    exit(3);
  }
  if (expect_ret == MJRPC_RET_OK && response == NULL) {
    fprintf(stderr, "benchmark setup error: empty response\n");
    exit(3);
  }
  bench_free(response);
}

static void process_ctx_destroy(process_ctx_t *c)
{
  if (c->handle != NULL) {
    mjrpc_destroy_handle(c->handle);
    c->handle = NULL;
  }
  bench_free(c->request);
  c->request = NULL;
}

/* ---- process_str_simple ------------------------------------------ */

static process_ctx_t g_simple_ctx;

static void setup_simple(void *ctx)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_echo, "bench_echo", NULL);
  c->request = bench_strdup(
      "{\"jsonrpc\":\"2.0\",\"method\":\"bench_echo\",\"id\":1}");
  process_sanity_check(c, MJRPC_RET_OK);
}

static void teardown_simple(void *ctx) { process_ctx_destroy((process_ctx_t *)ctx); }

/* ---- process_str_with_params -------------------------------------- */

static process_ctx_t g_params_ctx;

static void setup_params(void *ctx)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_sum, "bench_sum", NULL);
  c->request = bench_strdup(
      "{\"jsonrpc\":\"2.0\",\"method\":\"bench_sum\",\"params\":"
      "{\"a\":21,\"b\":21},\"id\":2}");
  process_sanity_check(c, MJRPC_RET_OK);
}

static void teardown_params(void *ctx) { process_ctx_destroy((process_ctx_t *)ctx); }

/* ---- process_str_notification -------------------------------------- */

static process_ctx_t g_notify_ctx;

static void setup_notification(void *ctx)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_echo, "bench_echo", NULL);
  c->request = bench_strdup(
      "{\"jsonrpc\":\"2.0\",\"method\":\"bench_echo\",\"params\":\"x\"}");
  process_sanity_check(c, MJRPC_RET_OK_NOTIFICATION);
}

static void teardown_notification(void *ctx)
{
  process_ctx_destroy((process_ctx_t *)ctx);
}

/* ---- process_str_method_not_found ---------------------------------- */

static process_ctx_t g_notfound_ctx;

static void setup_not_found(void *ctx)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_echo, "bench_echo", NULL);
  c->request = bench_strdup(
      "{\"jsonrpc\":\"2.0\",\"method\":\"no_such_method\",\"id\":3}");
  process_sanity_check(c, MJRPC_RET_OK);
}

static void teardown_not_found(void *ctx)
{
  process_ctx_destroy((process_ctx_t *)ctx);
}

/* ---- process_str_parse_error ---------------------------------------- */

static process_ctx_t g_parse_error_ctx;

static void setup_parse_error(void *ctx)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_echo, "bench_echo", NULL);
  c->request = bench_strdup("{\"jsonrpc\":\"2.0\",\"method\":");
  process_sanity_check(c, MJRPC_RET_ERROR_PARSE_FAILED);
}

static void teardown_parse_error(void *ctx)
{
  process_ctx_destroy((process_ctx_t *)ctx);
}

/* ---- process_cjson_direct (request tree pre-parsed) ----------------- */

typedef struct {
  mjrpc_handle_t *handle;
  cJSON *request; /* owned */
} process_cjson_ctx_t;

static process_cjson_ctx_t g_cjson_ctx;

static void setup_cjson_direct(void *ctx)
{
  process_cjson_ctx_t *c = (process_cjson_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_echo, "bench_echo", NULL);
  c->request = cJSON_Parse(
      "{\"jsonrpc\":\"2.0\",\"method\":\"bench_echo\",\"id\":4}");
  if (c->request == NULL) {
    fprintf(stderr, "benchmark setup error: cJSON_Parse failed\n");
    exit(3);
  }
}

static long long op_process_cjson_direct(void *ctx, long long iters)
{
  process_cjson_ctx_t *c = (process_cjson_ctx_t *)ctx;
  for (long long i = 0; i < iters; i++) {
    cJSON *response = mjrpc_process_cjson(c->handle, c->request, NULL);
    cJSON_Delete(response);
  }
  return iters;
}

static void teardown_cjson_direct(void *ctx)
{
  process_cjson_ctx_t *c = (process_cjson_ctx_t *)ctx;
  mjrpc_destroy_handle(c->handle);
  cJSON_Delete(c->request);
}

/* ---- batch requests -------------------------------------------------- */

static char *build_batch_request(int count)
{
  size_t capacity = (size_t)count * 64 + 16;
  char *buf = (char *)bench_malloc(capacity);
  if (buf == NULL) {
    return NULL;
  }
  size_t used = 0;
  used += (size_t)snprintf(buf + used, capacity - used, "[");
  for (int i = 0; i < count; i++) {
    used += (size_t)snprintf(
        buf + used, capacity - used,
        "%s{\"jsonrpc\":\"2.0\",\"method\":\"bench_echo\",\"id\":%d}",
        (i == 0) ? "" : ",", i + 1);
  }
  snprintf(buf + used, capacity - used, "]");
  return buf;
}

static process_ctx_t g_batch10_ctx;
static process_ctx_t g_batch100_ctx;

static void setup_batch10(void *ctx)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_echo, "bench_echo", NULL);
  c->request = build_batch_request(10);
  process_sanity_check(c, MJRPC_RET_OK);
}

static void teardown_batch10(void *ctx) { process_ctx_destroy((process_ctx_t *)ctx); }

static void setup_batch100(void *ctx)
{
  process_ctx_t *c = (process_ctx_t *)ctx;
  c->handle = mjrpc_create_handle(16);
  mjrpc_add_method(c->handle, cb_echo, "bench_echo", NULL);
  c->request = build_batch_request(100);
  process_sanity_check(c, MJRPC_RET_OK);
}

static void teardown_batch100(void *ctx) { process_ctx_destroy((process_ctx_t *)ctx); }

/* ---- full round trip -------------------------------------------------- */

static mjrpc_handle_t *g_roundtrip_handle;

static void setup_roundtrip(void *ctx)
{
  (void)ctx;
  g_roundtrip_handle = mjrpc_create_handle(16);
  mjrpc_add_method(g_roundtrip_handle, cb_sum, "bench_sum", NULL);
}

static long long op_roundtrip(void *ctx, long long iters)
{
  (void)ctx;
  int ret_code = 0;
  for (long long i = 0; i < iters; i++) {
    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
      break;
    }
    cJSON_AddNumberToObject(params, "a", 1.0);
    cJSON_AddNumberToObject(params, "b", 2.0);
    cJSON *id = cJSON_CreateNumber((double)i);
    char *request = mjrpc_request_str("bench_sum", params, id);
    char *response = mjrpc_process_str(g_roundtrip_handle, request, &ret_code);
    bench_free(request);
    bench_free(response);
  }
  return iters;
}

static void teardown_roundtrip(void *ctx)
{
  (void)ctx;
  mjrpc_destroy_handle(g_roundtrip_handle);
  g_roundtrip_handle = NULL;
}

/* ---- multi-threaded processing ----------------------------------------- */

#define BENCH_THREADS 4

/* portable spin barrier (pthread_barrier_t is not available on macOS) */
typedef struct {
  _Atomic long long arrived;
  long long total;
} spin_barrier_t;

static void barrier_wait(spin_barrier_t *barrier)
{
  atomic_fetch_add(&barrier->arrived, 1);
  while (atomic_load(&barrier->arrived) < barrier->total) {
    /* spin; the critical section is tiny */
  }
}

typedef struct {
  mjrpc_handle_t *handle;
  const char *request;
  long long ops;
} thread_worker_arg_t;

typedef struct {
  mjrpc_handle_t *handles[BENCH_THREADS];
  thread_worker_arg_t args[BENCH_THREADS];
  pthread_t tids[BENCH_THREADS];
  spin_barrier_t barrier;
} threads_ctx_t;

static threads_ctx_t g_threads_ctx;

static void *thread_worker_main(void *arg)
{
  thread_worker_arg_t *w = (thread_worker_arg_t *)arg;
  barrier_wait(&g_threads_ctx.barrier);
  int ret_code = 0;
  for (long long i = 0; i < w->ops; i++) {
    char *response = mjrpc_process_str(w->handle, w->request, &ret_code);
    bench_free(response);
  }
  return NULL;
}

static void setup_threads(void *ctx)
{
  threads_ctx_t *c = (threads_ctx_t *)ctx;
  for (int t = 0; t < BENCH_THREADS; t++) {
    c->handles[t] = mjrpc_create_handle(16);
    mjrpc_add_method(c->handles[t], cb_echo, "bench_echo", NULL);
    c->args[t].handle = c->handles[t];
    c->args[t].request =
        "{\"jsonrpc\":\"2.0\",\"method\":\"bench_echo\",\"id\":1}";
    c->args[t].ops = 0;
  }
  c->barrier.total = BENCH_THREADS + 1;
}

static long long ops_for_threads(long long iters)
{
  return (iters / BENCH_THREADS) * BENCH_THREADS;
}

static long long op_threads(void *ctx, long long iters)
{
  threads_ctx_t *c = (threads_ctx_t *)ctx;
  long long per_thread = iters / BENCH_THREADS;
  if (per_thread < 1) {
    per_thread = 1;
  }
  long long actual = per_thread * BENCH_THREADS;

  atomic_store(&c->barrier.arrived, 0);
  for (int t = 0; t < BENCH_THREADS; t++) {
    c->args[t].ops = per_thread;
    if (pthread_create(&c->tids[t], NULL, thread_worker_main, &c->args[t]) !=
        0) {
      fprintf(stderr, "benchmark error: pthread_create failed\n");
      exit(3);
    }
  }
  barrier_wait(&c->barrier); /* release all workers at once */
  for (int t = 0; t < BENCH_THREADS; t++) {
    pthread_join(c->tids[t], NULL);
  }
  return actual;
}

static void teardown_threads(void *ctx)
{
  threads_ctx_t *c = (threads_ctx_t *)ctx;
  for (int t = 0; t < BENCH_THREADS; t++) {
    mjrpc_destroy_handle(c->handles[t]);
    c->handles[t] = NULL;
  }
}

/* ================================================================== */
/*  Benchmark table                                                   */
/*                                                                    */
/*  The lower bounds are deliberately conservative (an order of       */
/*  magnitude below typical values) so they only catch catastrophic   */
/*  regressions and never fail because of noisy CI machines.          */
/* ================================================================== */

static bench_def_t g_benchmarks[] = {
    {"handle_create_destroy", "create + destroy a handle (16 slots)", NULL,
     op_handle_lifecycle, NULL, NULL, NULL, 10000},

    {"add_method_1000", "register 1000 methods into one handle (per batch)",
     setup_add_method, op_add_method, NULL, NULL, &g_add_method_ctx, 100},

    {"request_str_small", "build request string, no params", NULL,
     op_request_str_small, NULL, NULL, NULL, 50000},

    {"request_str_params", "build request string with 5-field params object",
     NULL, op_request_str_params, NULL, NULL, NULL, 20000},

    {"process_str_simple", "parse + dispatch + serialize, small request",
     setup_simple, op_process_str, teardown_simple, NULL, &g_simple_ctx, 10000},

    {"process_str_with_params", "request with object params (field lookup)",
     setup_params, op_process_str, teardown_params, NULL, &g_params_ctx,
     10000},

    {"process_str_notification", "notification request (no response)",
     setup_notification, op_process_str, teardown_notification, NULL,
     &g_notify_ctx, 10000},

    {"process_str_method_not_found", "error path: unknown method",
     setup_not_found, op_process_str, teardown_not_found, NULL,
     &g_notfound_ctx, 10000},

    {"process_str_parse_error", "error path: invalid JSON",
     setup_parse_error, op_process_str, teardown_parse_error, NULL,
     &g_parse_error_ctx, 10000},

    {"process_cjson_direct", "dispatch pre-parsed cJSON (no parse/serialize)",
     setup_cjson_direct, op_process_cjson_direct, teardown_cjson_direct, NULL,
     &g_cjson_ctx, 20000},

    {"process_str_batch_10", "batch request with 10 calls (per batch)",
     setup_batch10, op_process_str, teardown_batch10, NULL, &g_batch10_ctx,
     2000},

    {"process_str_batch_100", "batch request with 100 calls (per batch)",
     setup_batch100, op_process_str, teardown_batch100, NULL, &g_batch100_ctx,
     300},

    {"roundtrip", "request_str + process_str, full round trip", setup_roundtrip,
     op_roundtrip, teardown_roundtrip, NULL, NULL, 10000},

    {"process_str_4threads", "4 threads, one handle each (aggregate ops)",
     setup_threads, op_threads, teardown_threads, ops_for_threads,
     &g_threads_ctx, 30000},
};

#define BENCH_COUNT ((int)(sizeof(g_benchmarks) / sizeof(g_benchmarks[0])))

/* ================================================================== */
/*  Results                                                           */
/* ================================================================== */

typedef struct {
  const char *name;
  const char *description;
  double ns_per_op;     /* median across samples */
  double ns_per_op_min; /* fastest sample */
  double ns_per_op_sd;  /* stddev across samples */
  double cpu_ns_per_op; /* process CPU time, median */
  double ops_per_sec;
  double allocs_per_op;
  double bytes_per_op;
  long long peak_live_bytes; /* absolute peak while the op ran */
  long long live_bytes_delta; /* live bytes after - before (0 = leak free) */
  long long iters_per_sample;
  int samples;
  long long min_ops_per_sec;
  int floor_ok;
} bench_result_t;

static bench_result_t g_results[BENCH_COUNT];
static int g_result_count = 0;

static int g_floor_failures = 0;
static int g_leaks_detected = 0;

static void run_bench(const bench_def_t *def)
{
  double t_begin = now_sec(CLOCK_MONOTONIC);
  void *ctx = def->ctx;
  if (def->setup != NULL) {
    def->setup(ctx);
  }

  /* --- calibration: find an iteration count that lasts ~30 ms ------ */
  /* Growth per step is bounded (x4) so that a bogus clock reading can
   * never explode the iteration count into absurdly long runs. */
  long long iters = 1;
  double dt_last = 0.0;
  for (;;) {
    double t0 = now_sec(CLOCK_MONOTONIC);
    long long actual = def->op(ctx, iters);
    double dt = now_sec(CLOCK_MONOTONIC) - t0;
    dt_last = dt;
    if (actual <= 0) {
      fprintf(stderr, "benchmark error: %s executed 0 ops\n", def->name);
      exit(3);
    }
    if (g_trace) {
      fprintf(stderr, "[trace] %-28s calib iters=%-10lld dt=%.6fs\n",
              def->name, iters, dt);
    }
    if (dt >= g_calib_target_secs * 0.75) {
      break;
    }
    double dt_eff = dt > 1e-8 ? dt : 1e-8;
    double scale = g_calib_target_secs / dt_eff;
    if (scale > 4.0) {
      scale = 4.0;
    }
    if (scale < 1.25) {
      scale = 1.25;
    }
    double next = (double)iters * scale;
    if (next < (double)iters + 1.0) {
      next = (double)iters + 1.0;
    }
    if (next >= 1e7) {
      iters = 10000000LL; /* hard cap, ~ms for our fastest ops */
      break;
    }
    iters = (long long)next;
  }

  /* Corrective pass: never let a wildly overshooting sample through. */
  if (dt_last > 2.0 * g_calib_target_secs) {
    long long reduced = (long long)((double)iters * g_calib_target_secs /
                                    dt_last * 1.1);
    if (reduced < 1) {
      reduced = 1;
    }
    iters = reduced;
  }

  long long actual_ops =
      (def->ops_for != NULL) ? def->ops_for(iters) : iters;

  /* --- warmup ------------------------------------------------------ */
  double t_calib = now_sec(CLOCK_MONOTONIC);
  def->op(ctx, iters);
  def->op(ctx, iters);
  double t_warmup = now_sec(CLOCK_MONOTONIC);

  /* --- speed samples ------------------------------------------------ */
  if (g_samples < 1) {
    g_samples = 1;
  }
  double wall[64];
  double cpu[64];
  int n = g_samples > 64 ? 64 : g_samples;
  for (int s = 0; s < n; s++) {
    double c0 = now_sec(CLOCK_PROCESS_CPUTIME_ID);
    double w0 = now_sec(CLOCK_MONOTONIC);
    def->op(ctx, iters);
    double w1 = now_sec(CLOCK_MONOTONIC);
    double c1 = now_sec(CLOCK_PROCESS_CPUTIME_ID);
    wall[s] = (w1 - w0) / (double)actual_ops * 1e9;
    cpu[s] = (c1 - c0) / (double)actual_ops * 1e9;
  }

  double wall_med = median_of(wall, n);
  double wall_min = wall[0];
  double cpu_med = median_of(cpu, n);
  double t_samples = now_sec(CLOCK_MONOTONIC);
  double mean = 0.0;
  for (int s = 0; s < n; s++) {
    mean += wall[s];
  }
  mean /= (double)n;
  double sd = stddev_of(wall, n, mean);

  /* --- memory ------------------------------------------------------- */
  track_begin_phase();
  long long live_before = atomic_load(&g_live_bytes);
  long long allocs_before = atomic_load(&g_allocs);
  long long bytes_before = atomic_load(&g_alloc_bytes);

  def->op(ctx, iters);

  long long allocs_used = atomic_load(&g_allocs) - allocs_before;
  long long bytes_used = atomic_load(&g_alloc_bytes) - bytes_before;
  long long live_after = atomic_load(&g_live_bytes);
  long long peak = atomic_load(&g_peak_bytes);
  double t_memory = now_sec(CLOCK_MONOTONIC);

  /* --- record -------------------------------------------------------- */
  bench_result_t *r = &g_results[g_result_count++];
  r->name = def->name;
  r->description = def->description;
  r->ns_per_op = wall_med;
  r->ns_per_op_min = wall_min;
  r->ns_per_op_sd = sd;
  r->cpu_ns_per_op = cpu_med;
  r->ops_per_sec = wall_med > 0.0 ? 1e9 / wall_med : 0.0;
  r->allocs_per_op = (double)allocs_used / (double)actual_ops;
  r->bytes_per_op = (double)bytes_used / (double)actual_ops;
  r->peak_live_bytes = peak;
  r->live_bytes_delta = live_after - live_before;
  r->iters_per_sample = iters;
  r->samples = n;
  r->min_ops_per_sec = def->min_ops_per_sec;
  r->floor_ok = (def->min_ops_per_sec <= 0) ||
                (r->ops_per_sec >= (double)def->min_ops_per_sec);
  if (!r->floor_ok) {
    g_floor_failures++;
    fprintf(stderr,
            "  !! %s: %.0f ops/s is below the lower bound of %lld ops/s\n",
            def->name, r->ops_per_sec, def->min_ops_per_sec);
  }

  printf("  [%2d/%2d] %-28s %12.1f ns/op  %14.0f ops/s  %5.2f allocs/op  "
         "%7.1f B/op\n",
         g_result_count, BENCH_COUNT, r->name, r->ns_per_op, r->ops_per_sec,
         r->allocs_per_op, r->bytes_per_op);
  fflush(stdout);

  if (g_trace) {
    double now = now_sec(CLOCK_MONOTONIC);
    fprintf(stderr,
            "[trace] %-28s calib=%6.3fs warmup=%6.3fs samples=%6.3fs "
            "memory=%6.3fs total=%6.3fs\n",
            def->name, t_calib, t_warmup, t_samples, t_memory, now - t_begin);
  }

  if (def->teardown != NULL) {
    def->teardown(ctx);
  }
}

/* ================================================================== */
/*  Machine / process information                                     */
/* ================================================================== */

static char g_cpu_name[128] = "unknown";
static long g_cpu_cores = 0;
static long g_mem_total_mb = 0;

static void fill_cpu_name(void)
{
#if defined(__APPLE__)
  size_t len = sizeof(g_cpu_name);
  if (sysctlbyname("machdep.cpu.brand_string", g_cpu_name, &len, NULL, 0) !=
      0) {
    strncpy(g_cpu_name, "unknown", sizeof(g_cpu_name) - 1);
  }
#elif defined(__linux__)
  FILE *f = fopen("/proc/cpuinfo", "r");
  if (f != NULL) {
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
      if (strncmp(line, "model name", 10) == 0) {
        char *colon = strchr(line, ':');
        if (colon != NULL) {
          colon++;
          while (*colon == ' ') {
            colon++;
          }
          size_t len = strlen(colon);
          if (len > 0 && colon[len - 1] == '\n') {
            colon[len - 1] = '\0';
          }
          snprintf(g_cpu_name, sizeof(g_cpu_name), "%s", colon);
        }
        break;
      }
    }
    fclose(f);
  }
#endif
}

static void fill_machine_info(void)
{
  fill_cpu_name();

#if defined(_SC_NPROCESSORS_ONLN)
  g_cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif

#if defined(__APPLE__)
  uint64_t memsize = 0;
  size_t len = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) == 0) {
    g_mem_total_mb = (long)(memsize / (1024 * 1024));
  }
#elif defined(__linux__)
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && page_size > 0) {
    g_mem_total_mb = (long)((double)pages * (double)page_size / (1024.0 * 1024.0));
  }
#endif
}

static void format_timestamp(char *buf, size_t len)
{
  time_t now = time(NULL);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

/* ================================================================== */
/*  Reporting                                                          */
/* ================================================================== */

static void json_escape(FILE *f, const char *str)
{
  for (const char *p = str; *p != '\0'; p++) {
    switch (*p) {
    case '"':
      fputs("\\\"", f);
      break;
    case '\\':
      fputs("\\\\", f);
      break;
    case '\n':
      fputs("\\n", f);
      break;
    case '\r':
      fputs("\\r", f);
      break;
    case '\t':
      fputs("\\t", f);
      break;
    default:
      fputc(*p, f);
      break;
    }
  }
}

static const char *env_or(const char *name, const char *fallback)
{
  const char *value = getenv(name);
  return (value != NULL && value[0] != '\0') ? value : fallback;
}

static void write_json(const char *path, double wall_total_sec,
                       const struct rusage *ru, const struct utsname *uname_buf,
                       const char *timestamp)
{
  FILE *f = fopen(path, "w");
  if (f == NULL) {
    fprintf(stderr, "error: cannot open %s for writing\n", path);
    return;
  }

#if defined(__APPLE__)
  long peak_rss_kb = (long)(ru->ru_maxrss / 1024);
#else
  long peak_rss_kb = ru->ru_maxrss;
#endif

  double cpu_user = (double)ru->ru_utime.tv_sec + (double)ru->ru_utime.tv_usec * 1e-6;
  double cpu_sys = (double)ru->ru_stime.tv_sec + (double)ru->ru_stime.tv_usec * 1e-6;

  fprintf(f, "{\n");
  fprintf(f, "  \"schema\": \"mjsonrpc-benchmark/1\",\n");
  fprintf(f, "  \"meta\": {\n");
  fprintf(f, "    \"library_version\": \"%s\",\n", MJSONRPC_VERSION);
  fprintf(f, "    \"build_type\": \"%s\",\n", BENCH_BUILD_TYPE);
  fprintf(f, "    \"compiler\": \"");
  json_escape(f, __VERSION__);
  fprintf(f, "\",\n");
  fprintf(f, "    \"platform\": \"%s\",\n", uname_buf->sysname);
  fprintf(f, "    \"release\": \"%s\",\n", uname_buf->release);
  fprintf(f, "    \"arch\": \"%s\",\n", uname_buf->machine);
  fprintf(f, "    \"cpu\": \"");
  json_escape(f, g_cpu_name);
  fprintf(f, "\",\n");
  fprintf(f, "    \"cpu_cores\": %ld,\n", g_cpu_cores);
  fprintf(f, "    \"mem_total_mb\": %ld,\n", g_mem_total_mb);
  fprintf(f, "    \"samples_per_benchmark\": %d,\n", g_samples);
  fprintf(f, "    \"timestamp\": \"%s\",\n", timestamp);
  fprintf(f, "    \"commit\": \"%s\",\n", env_or("GITHUB_SHA", "local"));
  fprintf(f, "    \"ci_run\": \"%s\",\n", env_or("GITHUB_RUN_ID", "local"));
  fprintf(f, "    \"ref\": \"%s\"\n", env_or("GITHUB_REF_NAME", "local"));
  fprintf(f, "  },\n");

  fprintf(f, "  \"benchmarks\": [\n");
  for (int i = 0; i < g_result_count; i++) {
    const bench_result_t *r = &g_results[i];
    fprintf(f, "    {\n");
    fprintf(f, "      \"name\": \"%s\",\n", r->name);
    fprintf(f, "      \"description\": \"");
    json_escape(f, r->description);
    fprintf(f, "\",\n");
    fprintf(f, "      \"unit\": \"ops\",\n");
    fprintf(f, "      \"samples\": %d,\n", r->samples);
    fprintf(f, "      \"iters_per_sample\": %lld,\n", r->iters_per_sample);
    fprintf(f, "      \"ns_per_op_median\": %.2f,\n", r->ns_per_op);
    fprintf(f, "      \"ns_per_op_min\": %.2f,\n", r->ns_per_op_min);
    fprintf(f, "      \"ns_per_op_stddev\": %.2f,\n", r->ns_per_op_sd);
    fprintf(f, "      \"cpu_ns_per_op_median\": %.2f,\n", r->cpu_ns_per_op);
    fprintf(f, "      \"ops_per_sec\": %.2f,\n", r->ops_per_sec);
    fprintf(f, "      \"allocs_per_op\": %.3f,\n", r->allocs_per_op);
    fprintf(f, "      \"bytes_alloc_per_op\": %.1f,\n", r->bytes_per_op);
    fprintf(f, "      \"peak_live_bytes\": %lld,\n", r->peak_live_bytes);
    fprintf(f, "      \"live_bytes_delta\": %lld,\n", r->live_bytes_delta);
    fprintf(f, "      \"min_ops_per_sec\": %lld,\n", r->min_ops_per_sec);
    fprintf(f, "      \"floor_ok\": %s\n", r->floor_ok ? "true" : "false");
    fprintf(f, "    }%s\n", (i + 1 < g_result_count) ? "," : "");
  }
  fprintf(f, "  ],\n");

  fprintf(f, "  \"process\": {\n");
  fprintf(f, "    \"wall_sec\": %.2f,\n", wall_total_sec);
  fprintf(f, "    \"cpu_user_sec\": %.2f,\n", cpu_user);
  fprintf(f, "    \"cpu_sys_sec\": %.2f,\n", cpu_sys);
  fprintf(f, "    \"peak_rss_kb\": %ld,\n", peak_rss_kb);
  fprintf(f, "    \"minor_page_faults\": %ld,\n", ru->ru_minflt);
  fprintf(f, "    \"major_page_faults\": %ld,\n", ru->ru_majflt);
  fprintf(f, "    \"voluntary_ctx_switches\": %ld,\n", ru->ru_nvcsw);
  fprintf(f, "    \"involuntary_ctx_switches\": %ld\n", ru->ru_nivcsw);
  fprintf(f, "  },\n");

  fprintf(f, "  \"checks\": {\n");
  fprintf(f, "    \"floors_ok\": %s,\n", g_floor_failures == 0 ? "true" : "false");
  fprintf(f, "    \"leaks_detected\": %s,\n", g_leaks_detected ? "true" : "false");
  fprintf(f, "    \"all_ok\": %s\n",
          (g_floor_failures == 0 && !g_leaks_detected) ? "true" : "false");
  fprintf(f, "  }\n");
  fprintf(f, "}\n");
  fclose(f);
}

static void write_markdown(const char *path, double wall_total_sec,
                           const struct rusage *ru)
{
  FILE *f = fopen(path, "w");
  if (f == NULL) {
    fprintf(stderr, "error: cannot open %s for writing\n", path);
    return;
  }

#if defined(__APPLE__)
  long peak_rss_kb = (long)(ru->ru_maxrss / 1024);
#else
  long peak_rss_kb = ru->ru_maxrss;
#endif

  fprintf(f, "## mjsonrpc benchmark results\n\n");
  fprintf(f, "- Library: **%s** (%s build)\n", MJSONRPC_VERSION,
          BENCH_BUILD_TYPE);
  fprintf(f, "- Machine: %s (%ld cores, %ld MB RAM)\n", g_cpu_name,
          g_cpu_cores, g_mem_total_mb);
  fprintf(f, "- Samples per benchmark: %d\n\n", g_samples);

  fprintf(f, "| Benchmark | ns/op (median) | ops/s | CPU ns/op | allocs/op | "
             "bytes/op | peak live | status |\n");
  fprintf(f, "|---|---:|---:|---:|---:|---:|---:|---|\n");
  for (int i = 0; i < g_result_count; i++) {
    const bench_result_t *r = &g_results[i];
    fprintf(f, "| `%s` | %.1f | %.0f | %.1f | %.2f | %.1f | %lld B | %s |\n",
            r->name, r->ns_per_op, r->ops_per_sec, r->cpu_ns_per_op,
            r->allocs_per_op, r->bytes_per_op, r->peak_live_bytes,
            r->floor_ok ? "pass" : "**FAIL**");
  }

  fprintf(f, "\n### Process resource usage\n\n");
  fprintf(f, "| Metric | Value |\n|---|---:|\n");
  fprintf(f, "| Wall time | %.2f s |\n", wall_total_sec);
  fprintf(f,
          "| User CPU | %.2f s |\n",
          (double)ru->ru_utime.tv_sec + (double)ru->ru_utime.tv_usec * 1e-6);
  fprintf(f,
          "| System CPU | %.2f s |\n",
          (double)ru->ru_stime.tv_sec + (double)ru->ru_stime.tv_usec * 1e-6);
  fprintf(f, "| Peak RSS | %ld KB |\n", peak_rss_kb);
  fprintf(f, "| Page faults (minor/major) | %ld / %ld |\n", ru->ru_minflt,
          ru->ru_majflt);
  fprintf(f, "| Context switches (voluntary/involuntary) | %ld / %ld |\n",
          ru->ru_nvcsw, ru->ru_nivcsw);
  fprintf(f, "\nSelf checks: floors %s, memory leaks %s\n",
          g_floor_failures == 0 ? "**pass**" : "**FAIL**",
          g_leaks_detected ? "**DETECTED**" : "none");
  fclose(f);
}

static void print_console_report(double wall_total_sec,
                                 const struct rusage *ru)
{
  printf("\n");
  printf("======================================================================="
         "=========\n");
  printf("  Results (library %s, %s build)\n", MJSONRPC_VERSION,
         BENCH_BUILD_TYPE);
  printf("======================================================================="
         "=========\n");
  printf("  %-28s %12s %14s %10s %9s\n", "benchmark", "ns/op", "ops/s",
         "allocs/op", "B/op");
  printf("  %-28s %12s %14s %10s %9s\n", "----------------------------",
         "------------", "--------------", "---------", "---------");
  for (int i = 0; i < g_result_count; i++) {
    const bench_result_t *r = &g_results[i];
    printf("  %-28s %12.1f %14.0f %10.2f %9.1f\n", r->name, r->ns_per_op,
           r->ops_per_sec, r->allocs_per_op, r->bytes_per_op);
  }

#if defined(__APPLE__)
  long peak_rss_kb = (long)(ru->ru_maxrss / 1024);
#else
  long peak_rss_kb = ru->ru_maxrss;
#endif

  printf("\n  Process resource usage\n");
  printf("    wall time                          : %8.2f s\n", wall_total_sec);
  printf("    CPU time (user / system)           : %8.2f s / %.2f s\n",
         (double)ru->ru_utime.tv_sec + (double)ru->ru_utime.tv_usec * 1e-6,
         (double)ru->ru_stime.tv_sec + (double)ru->ru_stime.tv_usec * 1e-6);
  printf("    peak RSS                           : %8ld KB\n", peak_rss_kb);
  printf("    page faults (minor / major)        : %8ld / %ld\n", ru->ru_minflt,
         ru->ru_majflt);
  printf("    context switches (vol. / invol.)   : %8ld / %ld\n", ru->ru_nvcsw,
         ru->ru_nivcsw);

  printf("\n  Self checks\n");
  printf("    lower bounds (speed floors)        : %s\n",
         g_floor_failures == 0 ? "PASS" : "FAIL");
  printf("    memory leaks                       : %s\n",
         g_leaks_detected ? "DETECTED" : "none");
}

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

static void print_usage(const char *argv0)
{
  printf("Usage: %s [options]\n\n", argv0);
  printf("Options:\n");
  printf("  --json FILE      write machine-readable results to FILE\n");
  printf("  --markdown FILE  write a markdown summary to FILE\n");
  printf("  --check          exit non-zero if a self check fails "
         "(leaks / lower bounds)\n");
  printf("  --quick          fewer samples, shorter calibration (fast run)\n");
  printf("  --filter SUBSTR  only run benchmarks whose name contains SUBSTR\n");
  printf("  --help           show this help\n");
}

int main(int argc, char **argv)
{
  const char *json_path = NULL;
  const char *markdown_path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
      json_path = argv[++i];
    } else if (strcmp(argv[i], "--markdown") == 0 && i + 1 < argc) {
      markdown_path = argv[++i];
    } else if (strcmp(argv[i], "--check") == 0) {
      g_check_mode = 1;
    } else if (strcmp(argv[i], "--quick") == 0) {
      g_samples = 3;
      g_calib_target_secs = 0.010;
    } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
      g_filter = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "error: unknown option '%s'\n\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  /* Install memory hooks before any library use so that every
   * allocation made by cJSON / mjsonrpc is accounted for. */
  cJSON_Hooks hooks;
  hooks.malloc_fn = bench_malloc;
  hooks.free_fn = bench_free;
  cJSON_InitHooks(&hooks);
  mjrpc_set_memory_hooks(bench_malloc, bench_free, bench_strdup);

  fill_machine_info();

  g_trace = (getenv("BENCH_TRACE") != NULL);

  char timestamp[32];
  format_timestamp(timestamp, sizeof(timestamp));

  struct utsname uname_buf;
  if (uname(&uname_buf) != 0) {
    memset(&uname_buf, 0, sizeof(uname_buf));
    snprintf(uname_buf.sysname, sizeof(uname_buf.sysname), "unknown");
    snprintf(uname_buf.release, sizeof(uname_buf.release), "unknown");
    snprintf(uname_buf.machine, sizeof(uname_buf.machine), "unknown");
  }

  printf("mjsonrpc benchmark suite (library %s, %s build)\n", MJSONRPC_VERSION,
         BENCH_BUILD_TYPE);
  printf("CPU: %s (%ld cores), RAM: %ld MB, samples: %d\n\n", g_cpu_name,
         g_cpu_cores, g_mem_total_mb, g_samples);

  double wall_start = now_sec(CLOCK_MONOTONIC);

  for (int i = 0; i < BENCH_COUNT; i++) {
    const bench_def_t *def = &g_benchmarks[i];
    if (g_filter != NULL && strstr(def->name, g_filter) == NULL) {
      continue;
    }
    if (g_result_count >= BENCH_COUNT) {
      break;
    }
    run_bench(def);
  }

  double wall_total_sec = now_sec(CLOCK_MONOTONIC) - wall_start;

  /* ---- leak check ------------------------------------------------- */
  long long live = atomic_load(&g_live_bytes);
  long long allocs = atomic_load(&g_allocs);
  long long frees = atomic_load(&g_frees);
  if (live != 0 || allocs != frees) {
    g_leaks_detected = 1;
    fprintf(stderr,
            "\nLEAK DETECTED: live bytes=%lld, allocations=%lld, frees=%lld "
            "(diff=%lld)\n",
            live, allocs, frees, allocs - frees);
  }

  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);

  print_console_report(wall_total_sec, &ru);

  if (json_path != NULL) {
    write_json(json_path, wall_total_sec, &ru, &uname_buf, timestamp);
    printf("\n  JSON results written to %s\n", json_path);
  }
  if (markdown_path != NULL) {
    write_markdown(markdown_path, wall_total_sec, &ru);
    printf("  Markdown summary written to %s\n", markdown_path);
  }

  /* Restore default allocators (nothing borrowed may remain). */
  mjrpc_set_memory_hooks(NULL, NULL, NULL);
  cJSON_InitHooks(NULL);

  int failed = g_leaks_detected || (g_check_mode && g_floor_failures > 0);
  if (g_check_mode) {
    printf("\n  RESULT: %s\n", failed ? "FAIL" : "PASS");
  }

  return failed ? 2 : 0;
}
