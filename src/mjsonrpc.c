/*
    MIT License

    Copyright (c) 2026 Xiao

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.
 */

#include "mjsonrpc.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*--- memory management hooks ---*/

/* Thread-local storage for memory function pointers, default to standard
 * library functions */
static _Thread_local mjrpc_malloc_func g_mjrpc_malloc = NULL;
static _Thread_local mjrpc_free_func g_mjrpc_free = NULL;
static _Thread_local mjrpc_strdup_func g_mjrpc_strdup = NULL;

/*--- error logging hooks ---*/

/* Thread-local storage for error logging function pointer */
static _Thread_local mjrpc_error_log_func g_mjrpc_error_log = NULL;

/**
 * @brief Initialize memory and error logging hooks if not yet initialized
 * @internal
 */
static inline void init_memory_hooks_if_needed(void) {
  if (g_mjrpc_malloc == NULL)
    g_mjrpc_malloc = malloc;
  if (g_mjrpc_free == NULL)
    g_mjrpc_free = free;
  if (g_mjrpc_strdup == NULL)
    g_mjrpc_strdup = strdup;
}

/**
 * @brief Log an error message if error logging is enabled
 * @param message Error message to log
 * @param error_code Optional error code (pass 0 if not applicable)
 * @internal
 */
static inline void log_error(const char *message, int error_code) {
  if (g_mjrpc_error_log != NULL) {
    g_mjrpc_error_log(message, error_code);
  }
}

/*--- utility ---*/

enum method_state { EMPTY, OCCUPIED, DELETED };

/** @brief Hash table load factor threshold for resize */
#define HASH_LOAD_FACTOR 0.75

/** @brief Default hash table initial capacity */
#define DEFAULT_INITIAL_CAPACITY 16

/** @brief Hash multiplier for string hashing (djb2 algorithm) */
#define HASH_MULTIPLIER 33

/** @brief Second hash multiplier for double hashing */
#define HASH_MULTIPLIER2 17

/**
 * @brief Round a size_t up to the next power of two
 * @param n Value to round up (0 returns 1)
 * @return Next power of two >= n
 * @internal
 */
static size_t next_power_of_2(size_t n) {
  if (n == 0)
    return 1;
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFF
  n |= n >> 32;
#endif
  n++;
  return n;
}

/**
 * @brief Compute hash value for a string key using djb2 algorithm
 * @param key String key to hash (must not be NULL for valid hash)
 * @param capacity Hash table capacity
 * @return Hash value modulo capacity
 *
 * @note Uses djb2 algorithm: hash = hash * 33 + char
 *       This provides better distribution than simple multiplication
 */
static size_t hash(const char *key, size_t capacity) {
  if (key == NULL)
    return 0;
  size_t hash_value = 5381; /* Initial hash value (prime) */
  while (*key) {
    hash_value = ((hash_value << 5) + hash_value) + (unsigned char)(*key++);
  }
  return hash_value & (capacity - 1);
}

/**
 * @brief Compute second hash value for double hashing
 * @param key String key to hash
 * @param capacity Hash table capacity (must be a power of two)
 * @return Second hash value for probe step size in range [1, capacity-1]
 *
 * @note The returned step is forced to be odd. An odd step is always
 *       coprime with a power-of-two capacity, guaranteeing the probe
 *       sequence visits every slot before repeating.
 */
static size_t hash2(const char *key, size_t capacity) {
  if (key == NULL || capacity <= 1)
    return 1; /* Minimum step size */
  size_t hash_value = 0;
  while (*key) {
    hash_value = (hash_value * HASH_MULTIPLIER2) + (unsigned char)(*key++);
  }
  /* Step in [1, capacity-1]; force odd so it is coprime with any
   * power-of-two capacity.  With power-of-two cap, this always yields
   * step <= capacity - 1. */
  size_t step = 1 + (hash_value % (capacity - 1));
  if (step % 2 == 0)
    step++; /* make odd */
  if (step >= capacity)
    step = 1; /* defensive cap */
  return step;
}

/**
 * @brief Locate the byte array holding the slot states of a table
 *
 * The states are stored in a parallel byte array directly after the slot
 * array (both live in the same allocation). This keeps the payload of a slot
 * at three pointers (24 bytes) instead of 32 bytes while preserving pointer
 * alignment.
 * @internal
 */
static inline unsigned char *method_states(const struct mjrpc_method *methods,
                                           size_t capacity) {
  return (unsigned char *)(methods + capacity);
}

/**
 * @brief Find a slot for a key using double hashing
 *
 * Probes until either the key is found or a free slot is reached. Tombstones
 * (DELETED) are skipped during the search but the first one is remembered as
 * an insertion point, so deleted slots get reused without breaking probe
 * chains.
 *
 * @param methods Slot array
 * @param states Parallel state array
 * @param capacity Table capacity
 * @param key Key to look up
 * @param found Set to true when the key already exists
 * @return Index of the existing entry, or of the slot to insert into
 * @internal
 */
static size_t hash_find(const struct mjrpc_method *methods,
                        const unsigned char *states, size_t capacity,
                        const char *key, bool *found) {
  size_t index = hash(key, capacity);
  size_t step_size = 0;
  size_t probe_count = 0;
  size_t first_deleted = capacity;

  while (states[index] != EMPTY) {
    if (states[index] == OCCUPIED && strcmp(methods[index].name, key) == 0) {
      *found = true;
      return index;
    }
    if (states[index] == DELETED && first_deleted == capacity)
      first_deleted = index;
    probe_count++;
    if (probe_count >= capacity)
      break;
    if (step_size == 0)
      step_size = hash2(key, capacity);
    index = (index + step_size) & (capacity - 1);
  }
  *found = false;
  return first_deleted < capacity ? first_deleted : index;
}

/**
 * @brief Allocate the method table for a handle (no-op if already allocated)
 * @internal
 */
static bool ensure_table(mjrpc_handle_t *handle) {
  if (handle->methods != NULL)
    return true;
  const size_t bytes =
      handle->capacity * (sizeof(struct mjrpc_method) + sizeof(unsigned char));
  handle->methods = (struct mjrpc_method *)g_mjrpc_malloc(bytes);
  if (handle->methods == NULL) {
    log_error("Hash table memory allocation failed",
              MJRPC_RET_ERROR_MEM_ALLOC_FAILED);
    return false;
  }
  memset(handle->methods, 0, bytes);
  return true;
}

static int resize(mjrpc_handle_t *handle) {
  init_memory_hooks_if_needed();
  const size_t old_capacity = handle->capacity;
  struct mjrpc_method *old_methods = handle->methods;

  /* Check for potential overflow */
  if (old_capacity > SIZE_MAX / 2) {
    log_error("Hash table resize overflow", 0);
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
  }

  /* Lazily allocate the table on first use instead of doubling the
   * (non-existent) initial table. */
  if (old_methods == NULL)
    return ensure_table(handle) ? MJRPC_RET_OK
                                : MJRPC_RET_ERROR_MEM_ALLOC_FAILED;

  const size_t new_capacity = old_capacity * 2;
  if (new_capacity == 0)
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;

  const size_t new_bytes =
      new_capacity * (sizeof(struct mjrpc_method) + sizeof(unsigned char));
  struct mjrpc_method *new_methods =
      (struct mjrpc_method *)g_mjrpc_malloc(new_bytes);
  if (new_methods == NULL) {
    log_error("Hash table resize memory allocation failed",
              MJRPC_RET_ERROR_MEM_ALLOC_FAILED);
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
  }
  memset(new_methods, 0, new_bytes);

  const unsigned char *old_states =
      old_methods ? method_states(old_methods, old_capacity) : NULL;
  unsigned char *new_states = method_states(new_methods, new_capacity);

  /* Rehash by moving entries (name/func/arg pointers) directly into the new
   * table: no temporary duplications are needed, which keeps resizing cheap
   * even for large method tables. */
  for (size_t i = 0; i < old_capacity; i++) {
    if (old_states[i] != OCCUPIED)
      continue;
    size_t slot = hash(old_methods[i].name, new_capacity);
    size_t step_size = 0;
    while (new_states[slot] != EMPTY) {
      if (step_size == 0)
        step_size = hash2(old_methods[i].name, new_capacity);
      slot = (slot + step_size) & (new_capacity - 1);
    }
    new_methods[slot].name = old_methods[i].name;
    new_methods[slot].func = old_methods[i].func;
    new_methods[slot].arg = old_methods[i].arg;
    new_states[slot] = OCCUPIED;
  }

  handle->capacity = new_capacity;
  handle->methods = new_methods;
  g_mjrpc_free(old_methods);
  return MJRPC_RET_OK;
}

/**
 * @brief Look a method up by name (length-bounded, allows zero-copy spans)
 * @internal
 */
static bool method_get_n(const mjrpc_handle_t *handle, const char *key,
                         size_t key_len, mjrpc_func *func, void **arg) {
  if (handle == NULL || key == NULL || func == NULL || arg == NULL ||
      handle->methods == NULL) {
    return false;
  }

  size_t hash_value = 5381;
  size_t hash2_value = 0;
  for (size_t i = 0; i < key_len; i++) {
    const unsigned char c = (unsigned char)key[i];
    hash_value = ((hash_value << 5) + hash_value) + c;
    hash2_value = (hash2_value * HASH_MULTIPLIER2) + c;
  }

  const size_t capacity = handle->capacity;
  const size_t mask = capacity - 1;
  const unsigned char *states = method_states(handle->methods, capacity);
  size_t index = hash_value & mask;
  size_t step_size = 0;
  size_t probe_count = 0;

  /* Use double hashing for better distribution with high load factors */
  while (states[index] != EMPTY) {
    if (states[index] == OCCUPIED &&
        strncmp(handle->methods[index].name, key, key_len) == 0 &&
        handle->methods[index].name[key_len] == '\0') {
      *func = handle->methods[index].func;
      *arg = handle->methods[index].arg;
      return true;
    }
    probe_count++;
    if (probe_count >= capacity) {
      break; /* Table is full, key not found */
    }
    if (step_size == 0) {
      /* Step in [1, capacity-1], forced odd (coprime with the power-of-two
       * capacity) so the probe sequence visits every slot. */
      step_size = 1 + (hash2_value % (capacity - 1));
      if (step_size % 2 == 0)
        step_size++;
      if (step_size >= capacity)
        step_size = 1;
    }
    index = (index + step_size) & mask;
  }
  return false;
}

static bool method_get(const mjrpc_handle_t *handle, const char *key,
                       mjrpc_func *func, void **arg) {
  return method_get_n(handle, key, key ? strlen(key) : 0, func, arg);
}

/*--- fast JSON serialization ---
 *
 * The helpers below render a cJSON tree byte-for-byte like
 * cJSON_PrintUnformatted(), but compute the exact output size up-front and
 * write it into a single caller-provided buffer. This avoids the incremental
 * buffer growth and the per-element offset re-scans (strlen) of the generic
 * printer, which dominate the cost of small responses.
 */

/** @brief Marker returned when a value cannot be serialized */
#define JSON_LEN_FAILURE ((size_t)-1)

#define JSONRPC_RESULT_PREFIX "{\"jsonrpc\":\"2.0\",\"result\":"
#define JSONRPC_ERROR_PREFIX "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":"
#define JSONRPC_CODE_MSG_SEP ",\"message\":"
#define JSONRPC_DATA_SEP ",\"data\":"
#define JSONRPC_ID_SEP ",\"id\":"

static inline unsigned char json_decimal_point(void) {
  return (unsigned char)localeconv()->decimal_point[0];
}

static inline bool json_compare_double(double a, double b) {
  double max_val = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
  return (fabs(a - b) <= max_val * DBL_EPSILON);
}

/**
 * @brief Render an int like cJSON's @c "%d" without going through printf
 * @internal
 */
static size_t json_format_int(int value, char *out) {
  char tmp[12];
  size_t n = 0;
  const bool negative = value < 0;
  unsigned int magnitude =
      negative ? (unsigned int)(-(value + 1)) + 1u : (unsigned int)value;
  do {
    tmp[n++] = (char)('0' + (magnitude % 10u));
    magnitude /= 10u;
  } while (magnitude != 0u);
  if (negative)
    tmp[n++] = '-';
  for (size_t i = 0; i < n; i++)
    out[i] = tmp[n - 1 - i];
  return n;
}

/**
 * @brief Format a number exactly like cJSON's print_number()
 * @param out Buffer with room for at least 26 bytes
 * @return Number of characters written
 * @internal
 */
static size_t json_format_number(const cJSON *item, char *out) {
  const double d = item->valuedouble;
  if (isnan(d) || isinf(d)) {
    memcpy(out, "null", 4);
    return 4;
  }
  if (d == (double)item->valueint)
    return json_format_int(item->valueint, out);

  int length = snprintf(out, 26, "%1.15g", d);
  double test = 0.0;
  if (length < 0 || (sscanf(out, "%lg", &test) != 1) ||
      !json_compare_double(test, d)) {
    length = snprintf(out, 26, "%1.17g", d);
  }
  if (length < 0)
    return 0;
  /* Normalize the locale dependent decimal point like cJSON does. */
  const unsigned char decimal_point = json_decimal_point();
  if (decimal_point != '.') {
    for (int i = 0; i < length; i++) {
      if ((unsigned char)out[i] == decimal_point)
        out[i] = '.';
    }
  }
  return (size_t)length;
}

/**
 * @brief Length of a string once rendered as a JSON string literal
 * @internal
 */
static size_t json_escaped_len(const char *str) {
  size_t length = 2; /* opening and closing quote */
  if (str == NULL)
    return length;
  for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
    switch (*p) {
    case '"':
    case '\\':
    case '\b':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
      length += 2;
      break;
    default:
      length += (*p < 32) ? 6 : 1;
      break;
    }
  }
  return length;
}

/**
 * @brief Write a JSON string literal (escaped like cJSON)
 * @return Pointer past the written literal
 * @internal
 */
static char *json_write_string(char *out, const char *str) {
  static const char hex_digits[] = "0123456789abcdef";
  *out++ = '"';
  if (str == NULL) {
    *out++ = '"';
    return out;
  }
  for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
    const unsigned char c = *p;
    if (c < 32 || c == '"' || c == '\\') {
      *out++ = '\\';
      switch (c) {
      case '\\':
        *out++ = '\\';
        break;
      case '"':
        *out++ = '"';
        break;
      case '\b':
        *out++ = 'b';
        break;
      case '\f':
        *out++ = 'f';
        break;
      case '\n':
        *out++ = 'n';
        break;
      case '\r':
        *out++ = 'r';
        break;
      case '\t':
        *out++ = 't';
        break;
      default:
        *out++ = 'u';
        *out++ = '0';
        *out++ = '0';
        *out++ = hex_digits[(c >> 4) & 0x0F];
        *out++ = hex_digits[c & 0x0F];
        break;
      }
    } else {
      *out++ = (char)c;
    }
  }
  *out++ = '"';
  return out;
}

/**
 * @brief Compute the exact unformatted JSON length of a cJSON tree
 * @return Length in bytes, or @ref JSON_LEN_FAILURE for invalid values
 * @internal
 */
static size_t json_measure(const cJSON *item) {
  if (item == NULL)
    return JSON_LEN_FAILURE;
  switch (item->type & 0xFF) {
  case cJSON_NULL:
    return 4;
  case cJSON_False:
    return 5;
  case cJSON_True:
    return 4;
  case cJSON_Number: {
    char tmp[26];
    return json_format_number(item, tmp);
  }
  case cJSON_String:
    return json_escaped_len(item->valuestring);
  case cJSON_Raw:
    return item->valuestring != NULL ? strlen(item->valuestring)
                                     : JSON_LEN_FAILURE;
  case cJSON_Array: {
    size_t length = 2; /* brackets */
    bool first = true;
    for (const cJSON *child = item->child; child != NULL; child = child->next) {
      if (!first)
        length++;
      first = false;
      const size_t part = json_measure(child);
      if (part == JSON_LEN_FAILURE)
        return JSON_LEN_FAILURE;
      length += part;
      if (length < part)
        return JSON_LEN_FAILURE;
    }
    return length;
  }
  case cJSON_Object: {
    size_t length = 2; /* braces */
    bool first = true;
    for (const cJSON *child = item->child; child != NULL; child = child->next) {
      if (!first)
        length++;
      first = false;
      length += json_escaped_len(child->string);
      length += 1; /* colon */
      const size_t part = json_measure(child);
      if (part == JSON_LEN_FAILURE)
        return JSON_LEN_FAILURE;
      length += part;
      if (length < part)
        return JSON_LEN_FAILURE;
    }
    return length;
  }
  default:
    return JSON_LEN_FAILURE;
  }
}

/**
 * @brief Write the unformatted JSON of a cJSON tree
 * @return Pointer past the written value
 * @internal
 */
static char *json_write(char *out, const cJSON *item) {
  switch (item->type & 0xFF) {
  case cJSON_NULL:
    memcpy(out, "null", 4);
    return out + 4;
  case cJSON_False:
    memcpy(out, "false", 5);
    return out + 5;
  case cJSON_True:
    memcpy(out, "true", 4);
    return out + 4;
  case cJSON_Number: {
    char tmp[26];
    const size_t length = json_format_number(item, tmp);
    memcpy(out, tmp, length);
    return out + length;
  }
  case cJSON_String:
    return json_write_string(out, item->valuestring);
  case cJSON_Raw: {
    const size_t length = strlen(item->valuestring);
    memcpy(out, item->valuestring, length);
    return out + length;
  }
  case cJSON_Array: {
    *out++ = '[';
    bool first = true;
    for (const cJSON *child = item->child; child != NULL; child = child->next) {
      if (!first)
        *out++ = ',';
      first = false;
      out = json_write(out, child);
    }
    *out++ = ']';
    return out;
  }
  case cJSON_Object: {
    *out++ = '{';
    bool first = true;
    for (const cJSON *child = item->child; child != NULL; child = child->next) {
      if (!first)
        *out++ = ',';
      first = false;
      out = json_write_string(out, child->string);
      *out++ = ':';
      out = json_write(out, child);
    }
    *out++ = '}';
    return out;
  }
  default:
    return NULL;
  }
}

static bool key_equals_ignore_case(const char *left, const char *right) {
  /* ASCII-only case folding: this is the hot path for request parsing and
   * avoids the tolower() locale lookups of the C library. */
  while ((((unsigned char)*left) | 0x20) == (((unsigned char)*right) | 0x20)) {
    if (*left == '\0')
      return true;
    left++;
    right++;
  }
  return false;
}

/*--- request dispatch ---*/

/**
 * @brief Result of dispatching a single JSON-RPC request
 * @internal
 */
typedef struct {
  bool has_response;   /**< A response must be produced */
  bool is_error;       /**< Response is an error response */
  cJSON *result;       /**< Owned success result */
  int code;            /**< Error code */
  const char *message; /**< Error message (literal or owned) */
  bool message_owned;  /**< message must be released with g_mjrpc_free */
  cJSON *data;         /**< Owned error data (optional) */
  const cJSON *id;     /**< Borrowed request id (NULL if absent) */
  bool force_null_id;  /**< Emit a literal null id (invalid id type) */
} mjrpc_outcome_t;

static void outcome_free(mjrpc_outcome_t *out) {
  cJSON_Delete(out->result);
  cJSON_Delete(out->data);
  if (out->message_owned)
    g_mjrpc_free((void *)out->message);
  out->result = NULL;
  out->data = NULL;
  out->message = NULL;
  out->message_owned = false;
}

/**
 * @brief Dispatch one JSON-RPC request object and fill its outcome
 * @internal
 */
/**
 * @brief Dispatch a request from already extracted fields
 *
 * Shared by the cJSON tree path (@ref rpc_dispatch) and the zero-copy string
 * path (@ref dispatch_envelope).
 * @internal
 */
static void dispatch_request(const mjrpc_handle_t *handle, const cJSON *id,
                             bool id_invalid, bool version_ok,
                             const char *method, size_t method_len,
                             bool method_is_string, cJSON *params,
                             mjrpc_outcome_t *out) {
  memset(out, 0, sizeof(*out));

  if (id_invalid) {
    out->has_response = true;
    out->is_error = true;
    out->force_null_id = true;
    out->code = JSON_RPC_CODE_INVALID_REQUEST;
    out->message = "Invalid request received: 'id' member type error.";
    return;
  }

  out->id = id;

  if (!version_ok) {
    out->has_response = (id != NULL);
    out->is_error = true;
    out->code = JSON_RPC_CODE_INVALID_REQUEST;
    out->message = "Invalid request received: JSONRPC version error.";
    return;
  }

  if (!method_is_string) {
    out->has_response = (id != NULL);
    out->is_error = true;
    out->code = JSON_RPC_CODE_INVALID_REQUEST;
    out->message = "Invalid request received: No 'method' member.";
    return;
  }

  mjrpc_func func = NULL;
  void *arg = NULL;
  if (!method_get_n(handle, method, method_len, &func, &arg) || !func) {
    out->has_response = (id != NULL);
    out->is_error = true;
    out->code = JSON_RPC_CODE_METHOD_NOT_FOUND;
    out->message = "Method not found.";
    return;
  }

  /* Determine params type: 0=object, 1=array, 2=no params */
  int actual_params_type = 2;
  if (params != NULL)
    actual_params_type = cJSON_IsArray(params) ? 1 : 0;

  mjrpc_func_ctx_t ctx = {0};
  ctx.error_code = 0;
  ctx.error_message = NULL;
  ctx.error_data = NULL;
  ctx.params_type = actual_params_type;
  ctx.data = arg;

  cJSON *returned = func(&ctx, params, (cJSON *)id);
  if (ctx.error_code) {
    cJSON_Delete(returned);
    out->has_response = (id != NULL);
    out->is_error = true;
    out->code = ctx.error_code;
    out->message = ctx.error_message ? ctx.error_message : "No message here.";
    out->message_owned = (ctx.error_message != NULL);
    out->data = ctx.error_data;
    return;
  }

  if (ctx.error_data != NULL)
    cJSON_Delete(ctx.error_data);
  if (ctx.error_message != NULL)
    g_mjrpc_free(ctx.error_message);
  out->result = returned;
  out->has_response = (id != NULL && returned != NULL);
}

static void rpc_dispatch(const mjrpc_handle_t *handle, const cJSON *request,
                         mjrpc_outcome_t *out) {
  const cJSON *id = NULL;
  const cJSON *version = NULL;
  const cJSON *method = NULL;
  const cJSON *params = NULL;
  if (cJSON_IsObject(request)) {
    for (const cJSON *item = request->child; item != NULL; item = item->next) {
      if (item->string == NULL)
        continue;
      /* Fast first-character dispatch (ASCII case-insensitive). */
      switch (((unsigned char)item->string[0]) | 0x20) {
      case 'i':
        if (id == NULL && key_equals_ignore_case(item->string, "id"))
          id = item;
        break;
      case 'j':
        if (version == NULL && key_equals_ignore_case(item->string, "jsonrpc"))
          version = item;
        break;
      case 'm':
        if (method == NULL && key_equals_ignore_case(item->string, "method"))
          method = item;
        break;
      case 'p':
        if (params == NULL && key_equals_ignore_case(item->string, "params"))
          params = item;
        break;
      }
    }
  }

#ifdef cJSON_Int
  const bool id_valid = id == NULL || cJSON_IsNull(id) || cJSON_IsString(id) ||
                        (id->type & 0xFF) == cJSON_Int;
#else
  const bool id_valid =
      id == NULL || cJSON_IsNull(id) || cJSON_IsString(id) || cJSON_IsNumber(id);
#endif

  const bool version_ok =
      cJSON_IsString(version) && strcmp("2.0", version->valuestring) == 0;
  const bool method_is_string = cJSON_IsString(method);

  dispatch_request(handle, id, !id_valid, version_ok,
                   method_is_string ? method->valuestring : NULL,
                   method_is_string ? strlen(method->valuestring) : 0,
                   method_is_string, (cJSON *)params, out);
}

/**
 * @brief Render an outcome as a cJSON response tree
 * @internal
 */
static cJSON *outcome_to_cjson(mjrpc_outcome_t *out) {
  if (!out->has_response) {
    outcome_free(out);
    return NULL;
  }

  cJSON *id_copy;
  if (!out->force_null_id && out->id != NULL) {
    if (cJSON_IsString(out->id))
      id_copy = cJSON_CreateString(out->id->valuestring);
    else if (cJSON_IsNumber(out->id))
      id_copy = cJSON_CreateNumber(out->id->valuedouble);
    else
      id_copy = cJSON_CreateNull();
  } else {
    id_copy = cJSON_CreateNull();
  }

  cJSON *response;
  if (out->is_error) {
    response = mjrpc_response_error(out->code, out->message, id_copy);
    if (response != NULL && out->data != NULL) {
      cJSON *error = cJSON_GetObjectItem(response, "error");
      if (error != NULL) {
        if (!cJSON_AddItemToObject(error, "data", out->data))
          cJSON_Delete(out->data);
      } else {
        cJSON_Delete(out->data);
      }
    } else if (out->data != NULL) {
      cJSON_Delete(out->data);
    }
  } else {
    response = mjrpc_response_ok(out->result, id_copy);
  }

  out->result = NULL;
  out->data = NULL;
  if (out->message_owned)
    g_mjrpc_free((void *)out->message);
  out->message = NULL;
  out->message_owned = false;
  return response;
}

/**
 * @brief Length of the JSON-RPC envelope body of an outcome
 * @internal
 */
static size_t outcome_body_len(const mjrpc_outcome_t *out) {
  size_t length;
  char code_buffer[12];
  if (out->is_error) {
    const size_t code_len = json_format_int(out->code, code_buffer);
    const char *message = out->message ? out->message : "No message here.";
    length = (sizeof(JSONRPC_ERROR_PREFIX) - 1) + code_len +
             (sizeof(JSONRPC_CODE_MSG_SEP) - 1) + json_escaped_len(message);
    if (out->data != NULL) {
      const size_t data_len = json_measure(out->data);
      if (data_len == JSON_LEN_FAILURE)
        return JSON_LEN_FAILURE;
      length += (sizeof(JSONRPC_DATA_SEP) - 1) + data_len;
    }
    length += 1; /* closing brace of the error object */
  } else {
    const size_t result_len = json_measure(out->result);
    if (result_len == JSON_LEN_FAILURE)
      return JSON_LEN_FAILURE;
    length = (sizeof(JSONRPC_RESULT_PREFIX) - 1) + result_len;
  }
  return length;
}

static size_t outcome_id_len(const mjrpc_outcome_t *out) {
  if (!out->force_null_id && out->id != NULL) {
    const size_t length = json_measure(out->id);
    if (length != JSON_LEN_FAILURE)
      return length;
  }
  return 4; /* "null" */
}

/**
 * @brief Write the JSON-RPC envelope plus id of an outcome
 * @param out Destination (must have room for outcome_body_len + id + braces)
 * @internal
 */
static char *outcome_write(char *dst, const mjrpc_outcome_t *outcome,
                           size_t id_len) {
  (void)id_len;
  char code_buffer[12];
  if (outcome->is_error) {
    memcpy(dst, JSONRPC_ERROR_PREFIX, sizeof(JSONRPC_ERROR_PREFIX) - 1);
    dst += sizeof(JSONRPC_ERROR_PREFIX) - 1;
    const size_t code_len = json_format_int(outcome->code, code_buffer);
    memcpy(dst, code_buffer, code_len);
    dst += code_len;
    memcpy(dst, JSONRPC_CODE_MSG_SEP, sizeof(JSONRPC_CODE_MSG_SEP) - 1);
    dst += sizeof(JSONRPC_CODE_MSG_SEP) - 1;
    dst = json_write_string(
        dst, outcome->message ? outcome->message : "No message here.");
    if (outcome->data != NULL) {
      memcpy(dst, JSONRPC_DATA_SEP, sizeof(JSONRPC_DATA_SEP) - 1);
      dst += sizeof(JSONRPC_DATA_SEP) - 1;
      dst = json_write(dst, outcome->data);
    }
    *dst++ = '}';
  } else {
    memcpy(dst, JSONRPC_RESULT_PREFIX, sizeof(JSONRPC_RESULT_PREFIX) - 1);
    dst += sizeof(JSONRPC_RESULT_PREFIX) - 1;
    dst = json_write(dst, outcome->result);
  }
  memcpy(dst, JSONRPC_ID_SEP, sizeof(JSONRPC_ID_SEP) - 1);
  dst += sizeof(JSONRPC_ID_SEP) - 1;
  if (!outcome->force_null_id && outcome->id != NULL) {
    dst = json_write(dst, outcome->id);
  } else {
    memcpy(dst, "null", 4);
    dst += 4;
  }
  *dst++ = '}';
  return dst;
}

/**
 * @brief Render an outcome as a JSON response string
 * @return Newly allocated string (caller frees) or NULL
 * @internal
 */
static char *outcome_to_string(mjrpc_outcome_t *out) {
  if (!out->has_response) {
    outcome_free(out);
    return NULL;
  }

  const size_t body_len = outcome_body_len(out);
  const size_t id_len = outcome_id_len(out);
  if (body_len == JSON_LEN_FAILURE) {
    outcome_free(out);
    return NULL;
  }

  const size_t total = body_len + (sizeof(JSONRPC_ID_SEP) - 1) + id_len + 1;
  /* The returned string is handed to the caller, so it must come from
   * cJSON's (global) allocator, exactly like the strings produced by
   * cJSON_Print(). */
  char *buffer = (char *)cJSON_malloc(total + 1);
  if (buffer == NULL) {
    outcome_free(out);
    return NULL;
  }

  char *end = outcome_write(buffer, out, id_len);
  *end = '\0';
  outcome_free(out);
  return buffer;
}

/*--- cJSON response rendering (mjrpc_process_cjson) ---*/

static cJSON *rpc_handle_obj_req(const mjrpc_handle_t *handle,
                                 const cJSON *request) {
  mjrpc_outcome_t outcome;
  rpc_dispatch(handle, request, &outcome);
  return outcome_to_cjson(&outcome);
}

static cJSON *rpc_handle_ary_req(const mjrpc_handle_t *handle,
                                 const cJSON *request) {
  int valid_reqs = 0;
  cJSON *return_json_array = cJSON_CreateArray();
  if (return_json_array == NULL)
    return NULL;
  for (const cJSON *item = request->child; item != NULL; item = item->next) {
    cJSON *obj_req = rpc_handle_obj_req(handle, item);
    if (obj_req) {
      cJSON_AddItemToArray(return_json_array, obj_req);
      valid_reqs++;
    }
  }

  if (valid_reqs != 0)
    return return_json_array;
  // all requests are notifications or invalid
  cJSON_Delete(return_json_array);
  return NULL;
}

/**
 * @brief Render a standalone error response string
 * @internal
 */
static char *render_error_string(int code, const char *message,
                                const cJSON *id) {
  mjrpc_outcome_t outcome;
  memset(&outcome, 0, sizeof(outcome));
  outcome.has_response = true;
  outcome.is_error = true;
  outcome.code = code;
  outcome.message = message;
  outcome.id = id;
  outcome.force_null_id = (id == NULL);
  return outcome_to_string(&outcome);
}

/*--- string response rendering (mjrpc_process_str) ---*/

/** @brief Number of batch outcomes kept on the stack */
#define MJSONRPC_STACK_OUTCOMES 16

static char *rpc_handle_obj_req_str(const mjrpc_handle_t *handle,
                                    const cJSON *request) {
  mjrpc_outcome_t outcome;
  rpc_dispatch(handle, request, &outcome);
  return outcome_to_string(&outcome);
}

static char *rpc_handle_ary_req_str(const mjrpc_handle_t *handle,
                                    const cJSON *request, bool *had_response) {
  size_t count = 0;
  for (const cJSON *item = request->child; item != NULL; item = item->next)
    count++;
  if (count == 0) {
    *had_response = false;
    return NULL;
  }

  mjrpc_outcome_t stack_outcomes[MJSONRPC_STACK_OUTCOMES];
  mjrpc_outcome_t *outcomes = stack_outcomes;
  if (count > MJSONRPC_STACK_OUTCOMES) {
    outcomes = (mjrpc_outcome_t *)g_mjrpc_malloc(count * sizeof(*outcomes));
    if (outcomes == NULL) {
      *had_response = false;
      return NULL;
    }
  }

  size_t responders = 0;
  size_t total = 2; /* '[' and ']' */
  bool failed = false;
  size_t index = 0;
  for (const cJSON *item = request->child; item != NULL; item = item->next) {
    rpc_dispatch(handle, item, &outcomes[index]);
    if (outcomes[index].has_response) {
      const size_t body_len = outcome_body_len(&outcomes[index]);
      const size_t id_len = outcome_id_len(&outcomes[index]);
      if (body_len == JSON_LEN_FAILURE) {
        failed = true;
      } else {
        if (responders > 0)
          total++;
        total += body_len + (sizeof(JSONRPC_ID_SEP) - 1) + id_len + 1;
        responders++;
      }
    }
    index++;
  }

  if (failed || responders == 0) {
    for (size_t i = 0; i < count; i++)
      outcome_free(&outcomes[i]);
    if (outcomes != stack_outcomes)
      g_mjrpc_free(outcomes);
    *had_response = responders != 0;
    return NULL;
  }

  /* Response strings are owned by the caller: allocate them with cJSON's
   * allocator, matching cJSON_Print() output. */
  char *buffer = (char *)cJSON_malloc(total + 1);
  if (buffer == NULL) {
    for (size_t i = 0; i < count; i++)
      outcome_free(&outcomes[i]);
    if (outcomes != stack_outcomes)
      g_mjrpc_free(outcomes);
    *had_response = true;
    return NULL;
  }

  char *out = buffer;
  *out++ = '[';
  bool first = true;
  for (size_t i = 0; i < count; i++) {
    if (!outcomes[i].has_response)
      continue;
    if (!first)
      *out++ = ',';
    first = false;
    out = outcome_write(out, &outcomes[i], outcome_id_len(&outcomes[i]));
  }
  *out++ = ']';
  *out = '\0';

  for (size_t i = 0; i < count; i++)
    outcome_free(&outcomes[i]);
  if (outcomes != stack_outcomes)
    g_mjrpc_free(outcomes);
  *had_response = true;
  return buffer;
}
/*--- zero-copy request scanning (mjrpc_process_str fast path) ---
 *
 * The scanner below validates and dissects a JSON-RPC request string without
 * building a cJSON tree for the envelope. Method names, params and ids are
 * recorded as spans into the input buffer, so only the params/id values (which
 * are handed to the callbacks) are materialized as cJSON nodes.
 *
 * The scanner is conservative: it validates the JSON grammar strictly and
 * reports failure for anything unusual (escaped keys, exotic numbers, deep
 * nesting, trailing garbage, ...). On failure the caller falls back to the
 * regular cJSON based path, so the observable behavior is never different.
 */

/** @brief Maximum nesting depth accepted by the scanner before falling back */
#define RPC_SCAN_MAX_DEPTH 256

/** @brief Number of batch elements kept on the stack */
#define RPC_MAX_STACK_ENVELOPES 16

static const char *scan_skip_ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;
  return p;
}

static bool scan_hex(char c, unsigned int *out) {
  if (c >= '0' && c <= '9')
    *out = (unsigned int)(c - '0');
  else if (c >= 'a' && c <= 'f')
    *out = (unsigned int)(c - 'a' + 10);
  else if (c >= 'A' && c <= 'F')
    *out = (unsigned int)(c - 'A' + 10);
  else
    return false;
  return true;
}

/**
 * @brief Validate/scan a JSON string starting at its opening quote
 * @param has_escape Optional: set to true when escape sequences are present
 * @return Pointer past the closing quote, or NULL
 * @internal
 */
static const char *scan_string(const char *p, bool *has_escape) {
  if (*p != '"')
    return NULL;
  if (has_escape != NULL)
    *has_escape = false;
  p++;
  while (*p != '\0') {
    const unsigned char c = (unsigned char)*p;
    if (c == '"')
      return p + 1;
    if (c < 0x20)
      return NULL;
    if (c != '\\') {
      p++;
      continue;
    }
    if (has_escape != NULL)
      *has_escape = true;
    p++;
    switch (*p) {
    case '"':
    case '\\':
    case '/':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
      p++;
      break;
    case 'u': {
      p++;
      unsigned int code = 0;
      for (int i = 0; i < 4; i++) {
        unsigned int v;
        if (!scan_hex(p[i], &v))
          return NULL;
        code = (code << 4) | v;
      }
      p += 4;
      if (code >= 0xDC00 && code <= 0xDFFF)
        return NULL; /* lone low surrogate */
      if (code >= 0xD800 && code <= 0xDBFF) {
        /* high surrogate: a low surrogate escape must follow */
        if (p[0] != '\\' || p[1] != 'u')
          return NULL;
        p += 2;
        unsigned int low = 0;
        for (int i = 0; i < 4; i++) {
          unsigned int v;
          if (!scan_hex(p[i], &v))
            return NULL;
          low = (low << 4) | v;
        }
        p += 4;
        if (low < 0xDC00 || low > 0xDFFF)
          return NULL;
      }
      break;
    }
    default:
      return NULL;
    }
  }
  return NULL;
}

/**
 * @brief Scan a strict JSON number
 * @return Pointer past the number, or NULL
 * @internal
 */
static const char *scan_number(const char *p) {
  if (*p == '-')
    p++;
  if (*p == '0') {
    p++;
  } else if (*p >= '1' && *p <= '9') {
    while (*p >= '0' && *p <= '9')
      p++;
  } else {
    return NULL;
  }
  if (*p == '.') {
    p++;
    if (*p < '0' || *p > '9')
      return NULL;
    while (*p >= '0' && *p <= '9')
      p++;
  }
  if (*p == 'e' || *p == 'E') {
    p++;
    if (*p == '+' || *p == '-')
      p++;
    if (*p < '0' || *p > '9')
      return NULL;
    while (*p >= '0' && *p <= '9')
      p++;
  }
  return p;
}

static const char *scan_value_depth(const char *p, int depth);

/**
 * @brief Validate/scan an arbitrary JSON value
 * @return Pointer past the value, or NULL
 * @internal
 */
static const char *scan_value_depth(const char *p, int depth) {
  if (depth > RPC_SCAN_MAX_DEPTH)
    return NULL;
  p = scan_skip_ws(p);
  switch (*p) {
  case '"':
    return scan_string(p, NULL);
  case '{': {
    p++;
    p = scan_skip_ws(p);
    if (*p == '}') {
      return p + 1;
    }
    for (;;) {
      p = scan_skip_ws(p);
      if (*p != '"')
        return NULL;
      p = scan_string(p, NULL);
      if (p == NULL)
        return NULL;
      p = scan_skip_ws(p);
      if (*p != ':')
        return NULL;
      p++;
      p = scan_value_depth(p, depth + 1);
      if (p == NULL)
        return NULL;
      p = scan_skip_ws(p);
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == '}')
        return p + 1;
      return NULL;
    }
  }
  case '[': {
    p++;
    p = scan_skip_ws(p);
    if (*p == ']') {
      return p + 1;
    }
    for (;;) {
      p = scan_value_depth(p, depth + 1);
      if (p == NULL)
        return NULL;
      p = scan_skip_ws(p);
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == ']')
        return p + 1;
      return NULL;
    }
  }
  case 't':
    return strncmp(p, "true", 4) == 0 ? p + 4 : NULL;
  case 'f':
    return strncmp(p, "false", 5) == 0 ? p + 5 : NULL;
  case 'n':
    return strncmp(p, "null", 4) == 0 ? p + 4 : NULL;
  default:
    return scan_number(p);
  }
}

static const char *scan_value(const char *p) { return scan_value_depth(p, 1); }

/**
 * @brief Case-insensitive comparison of a raw key span against a lowercase
 *        literal (length must match exactly)
 * @internal
 */
static bool scan_key_is(const char *key, size_t len, const char *name) {
  size_t i = 0;
  while (name[i] != '\0') {
    if (i >= len || ((unsigned char)key[i] | 0x20) != (unsigned char)name[i])
      return false;
    i++;
  }
  return i == len;
}

/** @brief Kind of a scanned batch element */
typedef enum { RPC_ELEM_OBJECT, RPC_ELEM_SKIP } rpc_elem_kind_t;

/** @brief Kind of a scanned id member */
typedef enum {
  RPC_ID_NONE,
  RPC_ID_STRING,
  RPC_ID_NUMBER,
  RPC_ID_NULL,
  RPC_ID_INVALID
} rpc_id_kind_t;

/**
 * @brief Dissected JSON-RPC request envelope
 *
 * Spans point into the original request string; the cJSON nodes are filled
 * in later by materialize_envelope().
 * @internal
 */
typedef struct {
  rpc_elem_kind_t kind;
  size_t members;
  bool version_seen;
  bool version_ok;
  bool method_seen;
  bool method_is_string;
  const char *method;
  size_t method_len;
  bool params_seen;
  const char *params_span;
  size_t params_len;
  rpc_id_kind_t id_kind;
  const char *id_span;
  size_t id_len;
  cJSON *params_node; /* owned */
  cJSON *id_node;     /* owned */
} rpc_envelope_t;

static void envelope_free(rpc_envelope_t *env) {
  cJSON_Delete(env->params_node);
  cJSON_Delete(env->id_node);
  env->params_node = NULL;
  env->id_node = NULL;
}

/**
 * @brief Scan an object as a JSON-RPC request envelope
 * @return Pointer past the closing brace, or NULL on fallback
 * @internal
 */
static const char *scan_object_envelope(const char *p, rpc_envelope_t *env,
                                        int depth) {
  memset(env, 0, sizeof(*env));
  env->kind = RPC_ELEM_OBJECT;
  if (*p != '{')
    return NULL;
  p++;
  p = scan_skip_ws(p);
  if (*p == '}')
    return p + 1;

  for (;;) {
    p = scan_skip_ws(p);
    if (*p != '"')
      return NULL;
    bool key_escape = false;
    const char *key = p + 1;
    p = scan_string(p, &key_escape);
    if (p == NULL || key_escape)
      return NULL;
    const size_t key_len = (size_t)(p - 1 - key);
    p = scan_skip_ws(p);
    if (*p != ':')
      return NULL;
    p++;
    p = scan_skip_ws(p);
    env->members++;

    const unsigned char first =
        key_len > 0 ? (unsigned char)(key[0] | 0x20) : (unsigned char)0;
    if (first == 'i' && env->id_kind == RPC_ID_NONE &&
        scan_key_is(key, key_len, "id")) {
      const char *value_start = p;
      const char *value_end = scan_value_depth(p, depth + 1);
      if (value_end == NULL)
        return NULL;
      env->id_span = value_start;
      env->id_len = (size_t)(value_end - value_start);
      if (*value_start == '"')
        env->id_kind = RPC_ID_STRING;
      else if (*value_start == '-' ||
               (*value_start >= '0' && *value_start <= '9'))
        env->id_kind = RPC_ID_NUMBER;
      else if (value_start[0] == 'n')
        env->id_kind = RPC_ID_NULL;
      else
        env->id_kind = RPC_ID_INVALID;
      p = value_end;
    } else if (first == 'j' && !env->version_seen &&
               scan_key_is(key, key_len, "jsonrpc")) {
      env->version_seen = true;
      if (*p != '"') {
        env->version_ok = false;
        p = scan_value_depth(p, depth + 1);
        if (p == NULL)
          return NULL;
      } else {
        bool value_escape = false;
        const char *value = p + 1;
        const char *value_end = scan_string(p, &value_escape);
        if (value_end == NULL || value_escape)
          return NULL;
        env->version_ok = ((size_t)(value_end - 1 - value) == 3) &&
                          value[0] == '2' && value[1] == '.' &&
                          value[2] == '0';
        p = value_end;
      }
    } else if (first == 'm' && !env->method_seen &&
               scan_key_is(key, key_len, "method")) {
      env->method_seen = true;
      if (*p != '"') {
        env->method_is_string = false;
        p = scan_value_depth(p, depth + 1);
        if (p == NULL)
          return NULL;
      } else {
        bool value_escape = false;
        const char *value = p + 1;
        const char *value_end = scan_string(p, &value_escape);
        if (value_end == NULL || value_escape)
          return NULL;
        env->method_is_string = true;
        env->method = value;
        env->method_len = (size_t)(value_end - 1 - value);
        p = value_end;
      }
    } else if (first == 'p' && !env->params_seen &&
               scan_key_is(key, key_len, "params")) {
      env->params_seen = true;
      const char *value_start = p;
      const char *value_end = scan_value_depth(p, depth + 1);
      if (value_end == NULL)
        return NULL;
      env->params_span = value_start;
      env->params_len = (size_t)(value_end - value_start);
      p = value_end;
    } else {
      p = scan_value_depth(p, depth + 1);
      if (p == NULL)
        return NULL;
    }

    p = scan_skip_ws(p);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == '}')
      return p + 1;
    return NULL;
  }
}

/** @brief Plan describing a scanned request (single object or batch) */
typedef struct {
  bool is_batch;
  size_t count;
  size_t capacity;
  rpc_envelope_t *items;
  rpc_envelope_t stack_items[RPC_MAX_STACK_ENVELOPES];
  bool heap;
} rpc_scan_plan_t;

static void scan_plan_init(rpc_scan_plan_t *plan) {
  memset(plan, 0, sizeof(*plan));
  plan->items = plan->stack_items;
  plan->capacity = RPC_MAX_STACK_ENVELOPES;
}

static void scan_plan_free(rpc_scan_plan_t *plan) {
  if (plan->heap)
    g_mjrpc_free(plan->items);
  plan->items = plan->stack_items;
  plan->heap = false;
  plan->capacity = RPC_MAX_STACK_ENVELOPES;
  plan->count = 0;
}

static bool scan_plan_reserve(rpc_scan_plan_t *plan, size_t needed) {
  if (needed <= plan->capacity)
    return true;
  size_t new_capacity = plan->capacity;
  while (new_capacity < needed)
    new_capacity *= 2;
  rpc_envelope_t *new_items =
      (rpc_envelope_t *)g_mjrpc_malloc(new_capacity * sizeof(*new_items));
  if (new_items == NULL)
    return false;
  memcpy(new_items, plan->items, plan->count * sizeof(*new_items));
  if (plan->heap)
    g_mjrpc_free(plan->items);
  plan->items = new_items;
  plan->capacity = new_capacity;
  plan->heap = true;
  return true;
}

/**
 * @brief Scan a whole request string into a plan
 * @return false when the input must be handled by the generic path
 * @internal
 */
static bool scan_request(const char *str, rpc_scan_plan_t *plan) {
  scan_plan_init(plan);
  if (str == NULL)
    return false;

  const char *p = scan_skip_ws(str);
  if (*p == '{') {
    const char *end = scan_object_envelope(p, &plan->items[0], 1);
    if (end == NULL)
      goto fail;
    p = scan_skip_ws(end);
    if (*p != '\0')
      goto fail;
    plan->count = 1;
    return true;
  }

  if (*p != '[')
    goto fail;
  p++;
  p = scan_skip_ws(p);
  if (*p == ']')
    goto fail; /* empty array: generic path reports the error */

  for (;;) {
    if (!scan_plan_reserve(plan, plan->count + 1))
      goto fail;
    rpc_envelope_t *env = &plan->items[plan->count];
    p = scan_skip_ws(p);
    if (*p == '{') {
      const char *end = scan_object_envelope(p, env, 1);
      if (end == NULL)
        goto fail;
      p = end;
    } else {
      memset(env, 0, sizeof(*env));
      env->kind = RPC_ELEM_SKIP;
      const char *end = scan_value_depth(p, 1);
      if (end == NULL)
        goto fail;
      p = end;
    }
    plan->count++;
    p = scan_skip_ws(p);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == ']') {
      p++;
      break;
    }
    goto fail;
  }

  p = scan_skip_ws(p);
  if (*p != '\0')
    goto fail;
  plan->is_batch = true;
  return true;

fail:
  scan_plan_free(plan);
  return false;
}

/**
 * @brief Materialize the params/id spans of an envelope into cJSON nodes
 * @internal
 */
static bool materialize_envelope(rpc_envelope_t *env) {
  if (env->kind != RPC_ELEM_OBJECT)
    return true;
  if (env->params_seen) {
    const char *parse_end = NULL;
    cJSON *node = cJSON_ParseWithLengthOpts(env->params_span, env->params_len,
                                            &parse_end, 0);
    if (node == NULL || parse_end != env->params_span + env->params_len) {
      cJSON_Delete(node);
      return false;
    }
    env->params_node = node;
  }
  if (env->id_kind == RPC_ID_STRING || env->id_kind == RPC_ID_NUMBER ||
      env->id_kind == RPC_ID_NULL) {
    const char *parse_end = NULL;
    cJSON *node = cJSON_ParseWithLengthOpts(env->id_span, env->id_len,
                                            &parse_end, 0);
    if (node == NULL || parse_end != env->id_span + env->id_len) {
      cJSON_Delete(node);
      return false;
    }
    env->id_node = node;
  }
  return true;
}

static void dispatch_envelope(const mjrpc_handle_t *handle,
                              const rpc_envelope_t *env,
                              mjrpc_outcome_t *out) {
  if (env->kind != RPC_ELEM_OBJECT) {
    memset(out, 0, sizeof(*out));
    return;
  }
  dispatch_request(handle, env->id_node, env->id_kind == RPC_ID_INVALID,
                   env->version_ok, env->method, env->method_len,
                   env->method_is_string, env->params_node, out);
}

/**
 * @brief Fast path of mjrpc_process_str
 *
 * @return true when the request was fully handled (the generic path must be
 *         skipped); false when the caller has to fall back.
 * @note Once a callback has been dispatched the function never falls back, so
 *       methods cannot run twice.
 * @internal
 */
static bool process_str_fast(const mjrpc_handle_t *handle,
                             const char *request_str, int *ret_code,
                             char **response) {
  rpc_scan_plan_t plan;
  if (!scan_request(request_str, &plan))
    return false;

  if (!plan.is_batch && plan.items[0].members == 0) {
    scan_plan_free(&plan);
    *response = render_error_string(
        JSON_RPC_CODE_INVALID_REQUEST,
        "Invalid request received: Empty JSON object.", NULL);
    *ret_code = MJRPC_RET_ERROR_EMPTY_REQUEST;
    return true;
  }

  for (size_t i = 0; i < plan.count; i++) {
    if (!materialize_envelope(&plan.items[i])) {
      for (size_t j = 0; j < plan.count; j++)
        envelope_free(&plan.items[j]);
      scan_plan_free(&plan);
      return false; /* no callbacks ran yet: safe to fall back */
    }
  }

  if (!plan.is_batch) {
    mjrpc_outcome_t outcome;
    dispatch_envelope(handle, &plan.items[0], &outcome);
    *ret_code = outcome.has_response ? MJRPC_RET_OK : MJRPC_RET_OK_NOTIFICATION;
    *response = outcome_to_string(&outcome);
    envelope_free(&plan.items[0]);
    scan_plan_free(&plan);
    return true;
  }

  mjrpc_outcome_t stack_outcomes[RPC_MAX_STACK_ENVELOPES];
  mjrpc_outcome_t *outcomes = stack_outcomes;
  if (plan.count > RPC_MAX_STACK_ENVELOPES) {
    outcomes =
        (mjrpc_outcome_t *)g_mjrpc_malloc(plan.count * sizeof(*outcomes));
    if (outcomes == NULL) {
      for (size_t j = 0; j < plan.count; j++)
        envelope_free(&plan.items[j]);
      scan_plan_free(&plan);
      return false; /* still before dispatch: safe to fall back */
    }
  }

  size_t responders = 0;
  size_t total = 2; /* '[' and ']' */
  bool failed = false;
  for (size_t i = 0; i < plan.count; i++) {
    dispatch_envelope(handle, &plan.items[i], &outcomes[i]);
    if (!outcomes[i].has_response)
      continue;
    const size_t body_len = outcome_body_len(&outcomes[i]);
    const size_t id_len = outcome_id_len(&outcomes[i]);
    if (body_len == JSON_LEN_FAILURE) {
      failed = true;
      continue;
    }
    if (responders > 0)
      total++;
    total += body_len + (sizeof(JSONRPC_ID_SEP) - 1) + id_len + 1;
    responders++;
  }

  char *buffer = NULL;
  if (!failed && responders != 0) {
    buffer = (char *)cJSON_malloc(total + 1);
    if (buffer != NULL) {
      char *out = buffer;
      *out++ = '[';
      bool first = true;
      for (size_t i = 0; i < plan.count; i++) {
        if (!outcomes[i].has_response)
          continue;
        if (!first)
          *out++ = ',';
        first = false;
        out = outcome_write(out, &outcomes[i], outcome_id_len(&outcomes[i]));
      }
      *out++ = ']';
      *out = '\0';
    }
  }

  for (size_t i = 0; i < plan.count; i++)
    outcome_free(&outcomes[i]);
  for (size_t i = 0; i < plan.count; i++)
    envelope_free(&plan.items[i]);
  if (outcomes != stack_outcomes)
    g_mjrpc_free(outcomes);
  scan_plan_free(&plan);

  *response = buffer;
  /* A response exists (even if printing it failed) whenever there was at
   * least one responder. */
  *ret_code = (failed || responders != 0) ? MJRPC_RET_OK
                                          : MJRPC_RET_OK_NOTIFICATION;
  return true;
}


/*--- main functions ----*/

cJSON *mjrpc_request_cjson(const char *method, cJSON *params, cJSON *id) {
  if (method == NULL) {
    cJSON_Delete(params);
    cJSON_Delete(id);
    return NULL;
  }

  cJSON *json = cJSON_CreateObject();
  if (json == NULL) {
    cJSON_Delete(params);
    cJSON_Delete(id);
    return NULL;
  }

  cJSON_AddItemToObjectCS(json, "jsonrpc",
                          cJSON_CreateStringReference("2.0"));
  cJSON_AddItemToObjectCS(json, "method", cJSON_CreateString(method));
  if (params != NULL)
    cJSON_AddItemToObjectCS(json, "params", params);
  if (id != NULL)
    cJSON_AddItemToObjectCS(json, "id", id);

  return json;
}

char *mjrpc_request_str(const char *method, cJSON *params, cJSON *id) {
  init_memory_hooks_if_needed();
  if (method == NULL) {
    cJSON_Delete(params);
    cJSON_Delete(id);
    return NULL;
  }

  /* Build the request string directly: this avoids materializing a cJSON
   * envelope tree and running the generic printer over it, and sizes the
   * output buffer exactly. */
  const char *prefix = "{\"jsonrpc\":\"2.0\",\"method\":";
  size_t length = (sizeof("{\"jsonrpc\":\"2.0\",\"method\":") - 1) +
                  json_escaped_len(method);
  if (params != NULL) {
    const size_t params_len = json_measure(params);
    if (params_len == JSON_LEN_FAILURE)
      goto fail;
    length += (sizeof(",\"params\":") - 1) + params_len;
  }
  if (id != NULL) {
    const size_t id_len = json_measure(id);
    if (id_len == JSON_LEN_FAILURE)
      goto fail;
    length += (sizeof(",\"id\":") - 1) + id_len;
  }
  length += 1; /* closing brace */

  char *json_str = (char *)cJSON_malloc(length + 1);
  if (json_str == NULL)
    goto fail;

  {
    char *out = json_str;
    memcpy(out, prefix, (size_t)(sizeof("{\"jsonrpc\":\"2.0\",\"method\":") - 1));
    out += sizeof("{\"jsonrpc\":\"2.0\",\"method\":") - 1;
    out = json_write_string(out, method);
    if (params != NULL) {
      memcpy(out, ",\"params\":", sizeof(",\"params\":") - 1);
      out += sizeof(",\"params\":") - 1;
      out = json_write(out, params);
    }
    if (id != NULL) {
      memcpy(out, ",\"id\":", sizeof(",\"id\":") - 1);
      out += sizeof(",\"id\":") - 1;
      out = json_write(out, id);
    }
    *out++ = '}';
    *out = '\0';
  }

  cJSON_Delete(params);
  cJSON_Delete(id);
  // Note: json_str must be freed by caller (free() with default hooks / cJSON_free)
  return json_str;

fail:
  cJSON_Delete(params);
  cJSON_Delete(id);
  return NULL;
}

cJSON *mjrpc_response_ok(cJSON *result, cJSON *id) {
  if (id == NULL || result == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(id);
    return NULL;
  }

  cJSON *result_root = cJSON_CreateObject();
  if (result_root == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(id);
    return NULL;
  }

  cJSON_AddItemToObjectCS(result_root, "jsonrpc",
                          cJSON_CreateStringReference("2.0"));
  cJSON_AddItemToObjectCS(result_root, "result", result);
  cJSON_AddItemToObjectCS(result_root, "id", id);

  return result_root;
}

cJSON *mjrpc_response_error(int code, const char *message, cJSON *id) {
  init_memory_hooks_if_needed();

  cJSON *result_root = cJSON_CreateObject();
  cJSON *error_root = cJSON_CreateObject();
  if (result_root == NULL || error_root == NULL || id == NULL) {
    cJSON_Delete(id);
    cJSON_Delete(error_root);
    cJSON_Delete(result_root);
    return NULL;
  }

  cJSON_AddItemToObjectCS(error_root, "code", cJSON_CreateNumber(code));
  cJSON_AddItemToObjectCS(
      error_root, "message",
      cJSON_CreateString(message ? message : "No message here."));

  cJSON_AddItemToObjectCS(result_root, "jsonrpc",
                          cJSON_CreateStringReference("2.0"));
  cJSON_AddItemToObjectCS(result_root, "error", error_root);
  cJSON_AddItemToObjectCS(result_root, "id", id);

  return result_root;
}

mjrpc_handle_t *mjrpc_create_handle(size_t initial_capacity) {
  init_memory_hooks_if_needed();
  if (initial_capacity == 0)
    initial_capacity = DEFAULT_INITIAL_CAPACITY;
  initial_capacity = next_power_of_2(initial_capacity);
  mjrpc_handle_t *handle = g_mjrpc_malloc(sizeof(mjrpc_handle_t));
  if (handle == NULL)
    return NULL;
  handle->capacity = initial_capacity;
  handle->size = 0;
  /* The method table itself is allocated lazily on the first insertion, so
   * creating and destroying an unused handle stays as cheap as one single
   * allocation. */
  handle->methods = NULL;
  return handle;
}

int mjrpc_destroy_handle(mjrpc_handle_t *handle) {
  init_memory_hooks_if_needed();
  if (handle == NULL)
    return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
  if (handle->methods != NULL) {
    const unsigned char *states =
        method_states(handle->methods, handle->capacity);
    for (size_t i = 0; i < handle->capacity; i++) {
      if (states[i] == OCCUPIED) {
        g_mjrpc_free(handle->methods[i].name);
        if (handle->methods[i].arg != NULL)
          g_mjrpc_free(handle->methods[i].arg);
      }
    }
    g_mjrpc_free(handle->methods);
  }
  g_mjrpc_free(handle);
  return MJRPC_RET_OK;
}

int mjrpc_add_method(mjrpc_handle_t *handle, mjrpc_func function_pointer,
                     const char *method_name, void *arg2func) {
  init_memory_hooks_if_needed();
  if (handle == NULL)
    return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
  if (function_pointer == NULL || method_name == NULL)
    return MJRPC_RET_ERROR_INVALID_PARAM;

  /* Check load factor and resize if needed */
  if ((double)handle->size / (double)handle->capacity >= HASH_LOAD_FACTOR) {
    int resize_result = resize(handle);
    if (resize_result != MJRPC_RET_OK)
      return resize_result;
  }

  /* Lazily allocate the table on first insertion */
  if (!ensure_table(handle))
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;

  bool found = false;
  size_t index = hash_find(handle->methods,
                           method_states(handle->methods, handle->capacity),
                           handle->capacity, method_name, &found);
  if (found) {
    /* Method already exists, update it and free old arg if exists */
    if (handle->methods[index].arg != NULL) {
      g_mjrpc_free(handle->methods[index].arg);
    }
    handle->methods[index].func = function_pointer;
    handle->methods[index].arg = arg2func;
    return MJRPC_RET_OK;
  }

  handle->methods[index].name = g_mjrpc_strdup(method_name);
  if (handle->methods[index].name == NULL) {
    log_error("strdup failed during add_method",
              MJRPC_RET_ERROR_MEM_ALLOC_FAILED);
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
  }
  handle->methods[index].func = function_pointer;
  handle->methods[index].arg = arg2func;
  method_states(handle->methods, handle->capacity)[index] = OCCUPIED;
  handle->size++;
  return MJRPC_RET_OK;
}

int mjrpc_del_method(mjrpc_handle_t *handle, const char *name) {
  init_memory_hooks_if_needed();
  if (handle == NULL)
    return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
  if (name == NULL)
    return MJRPC_RET_ERROR_INVALID_PARAM;
  if (handle->methods == NULL)
    return MJRPC_RET_ERROR_NOT_FOUND;

  size_t index = hash(name, handle->capacity);
  size_t step_size = 0;
  size_t probe_count = 0;
  unsigned char *states =
      method_states(handle->methods, handle->capacity);

  /* Use double hashing for better distribution with high load factors */
  while (states[index] != EMPTY) {
    if (states[index] == OCCUPIED &&
        strcmp(handle->methods[index].name, name) == 0) {
      g_mjrpc_free(handle->methods[index].name);
      handle->methods[index].name = NULL;
      if (handle->methods[index].arg != NULL) {
        g_mjrpc_free(handle->methods[index].arg);
        handle->methods[index].arg = NULL;
      }
      states[index] = DELETED;
      handle->size--;
      return MJRPC_RET_OK;
    }
    probe_count++;
    if (probe_count >= handle->capacity) {
      break; /* Table is full, key not found */
    }
    /* Capacity is a power of two, so masking avoids integer division. */
    if (step_size == 0) {
      step_size =
          hash2(name, handle->capacity); /* Compute step size on first probe */
    }
    index = (index + step_size) & (handle->capacity - 1);
  }
  return MJRPC_RET_ERROR_NOT_FOUND;
}

size_t mjrpc_get_method_count(const mjrpc_handle_t *handle) {
  if (handle == NULL)
    return 0;
  return handle->size;
}

int mjrpc_enum_methods(const mjrpc_handle_t *handle,
                       void (*callback)(const char *method_name, void *arg,
                                        void *user_data),
                       void *user_data) {
  if (handle == NULL)
    return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
  if (callback == NULL)
    return MJRPC_RET_ERROR_INVALID_PARAM;
  if (handle->methods == NULL)
    return MJRPC_RET_OK;

  const unsigned char *states =
      method_states(handle->methods, handle->capacity);
  for (size_t i = 0; i < handle->capacity; i++) {
    if (states[i] == OCCUPIED) {
      callback(handle->methods[i].name, handle->methods[i].arg, user_data);
    }
  }
  return MJRPC_RET_OK;
}

char *mjrpc_process_str(const mjrpc_handle_t *handle, const char *request_str,
                        int *ret_code) {
  init_memory_hooks_if_needed();

  /* Fast path: validate/dissect the envelope without materializing a cJSON
   * request tree. Falls back to the generic path for anything unusual. */
  if (handle != NULL && request_str != NULL) {
    char *fast_response = NULL;
    int fast_ret = MJRPC_RET_OK;
    if (process_str_fast(handle, request_str, &fast_ret, &fast_response)) {
      if (ret_code)
        *ret_code = fast_ret;
      return fast_response;
    }
  }

  cJSON *request = cJSON_Parse(request_str);
  if (request == NULL) {
    /* Parse failed, create error response */
    if (ret_code) {
      *ret_code = MJRPC_RET_ERROR_PARSE_FAILED;
    }
    return render_error_string(
        JSON_RPC_CODE_PARSE_ERROR,
        "Invalid request received: Not a JSON formatted request.", NULL);
  }

  if (handle == NULL) {
    if (ret_code)
      *ret_code = MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
    cJSON_Delete(request);
    return NULL;
  }

  char *response_str = NULL;
  int ret = MJRPC_RET_OK;
  if (cJSON_IsArray(request)) {
    int array_size = cJSON_GetArraySize(request);
    if (array_size <= 0) {
      ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
      response_str = render_error_string(
          JSON_RPC_CODE_INVALID_REQUEST,
          "Invalid request received: Empty JSON array.", NULL);
    } else {
      bool had_response = false;
      response_str = rpc_handle_ary_req_str(handle, request, &had_response);
      ret = had_response ? MJRPC_RET_OK : MJRPC_RET_OK_NOTIFICATION;
    }
  } else if (cJSON_IsObject(request)) {
    if (request->child == NULL) {
      ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
      response_str = render_error_string(
          JSON_RPC_CODE_INVALID_REQUEST,
          "Invalid request received: Empty JSON object.", NULL);
    } else {
      mjrpc_outcome_t outcome;
      rpc_dispatch(handle, request, &outcome);
      ret = outcome.has_response ? MJRPC_RET_OK : MJRPC_RET_OK_NOTIFICATION;
      response_str = outcome_to_string(&outcome);
    }
  } else {
    ret = MJRPC_RET_ERROR_NOT_OBJ_ARY;
    response_str = render_error_string(
        JSON_RPC_CODE_INVALID_REQUEST,
        "Invalid request received: Not a JSON object or array.", NULL);
  }

  cJSON_Delete(request);
  if (ret_code)
    *ret_code = ret;
  return response_str;
}

cJSON *mjrpc_process_cjson(const mjrpc_handle_t *handle,
                           const cJSON *request_cjson, int *ret_code) {
  init_memory_hooks_if_needed();
  int ret = MJRPC_RET_OK;
  if (handle == NULL) {
    ret = MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
    if (ret_code)
      *ret_code = ret;
    return NULL;
  }

  if (request_cjson == NULL) {
    ret = MJRPC_RET_ERROR_PARSE_FAILED;
    if (ret_code)
      *ret_code = ret;
    return mjrpc_response_error(
        JSON_RPC_CODE_PARSE_ERROR,
        "Invalid request received: Not a JSON formatted request.",
        cJSON_CreateNull());
  }

  cJSON *cjson_return = NULL;
  if (cJSON_IsArray(request_cjson)) {
    int array_size = cJSON_GetArraySize(request_cjson);
    if (array_size <= 0) {
      ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
      cjson_return = mjrpc_response_error(
          JSON_RPC_CODE_INVALID_REQUEST,
          "Invalid request received: Empty JSON array.", cJSON_CreateNull());
    } else {
      cjson_return = rpc_handle_ary_req(handle, request_cjson);
      if (cjson_return)
        ret = MJRPC_RET_OK;
      else
        ret = MJRPC_RET_OK_NOTIFICATION;
    }
  } else if (cJSON_IsObject(request_cjson)) {
    if (request_cjson->child == NULL) {
      ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
      cjson_return = mjrpc_response_error(
          JSON_RPC_CODE_INVALID_REQUEST,
          "Invalid request received: Empty JSON object.", cJSON_CreateNull());
    } else {
      cjson_return = rpc_handle_obj_req(handle, request_cjson);
      if (cjson_return)
        ret = MJRPC_RET_OK;
      else
        ret = MJRPC_RET_OK_NOTIFICATION;
    }
  } else {
    cjson_return = mjrpc_response_error(
        JSON_RPC_CODE_INVALID_REQUEST,
        "Invalid request received: Not a JSON object or array.",
        cJSON_CreateNull());
    ret = MJRPC_RET_ERROR_NOT_OBJ_ARY;
  }
  if (ret_code)
    *ret_code = ret;
  return cjson_return;
}

int mjrpc_set_memory_hooks(mjrpc_malloc_func malloc_func,
                           mjrpc_free_func free_func,
                           mjrpc_strdup_func strdup_func) {
  /* Initialize defaults if not yet initialized */
  init_memory_hooks_if_needed();

  /* If all parameters are NULL, reset to default functions */
  if (malloc_func == NULL && free_func == NULL && strdup_func == NULL) {
    g_mjrpc_malloc = malloc;
    g_mjrpc_free = free;
    g_mjrpc_strdup = strdup;
    return MJRPC_RET_OK;
  }

  /* If any parameter is not NULL, all must be provided */
  if (malloc_func == NULL || free_func == NULL || strdup_func == NULL) {
    return MJRPC_RET_ERROR_INVALID_PARAM;
  }

  /* Set custom functions */
  g_mjrpc_malloc = malloc_func;
  g_mjrpc_free = free_func;
  g_mjrpc_strdup = strdup_func;

  return MJRPC_RET_OK;
}

int mjrpc_set_error_log_hook(mjrpc_error_log_func error_log_func) {
  g_mjrpc_error_log = error_log_func;
  return MJRPC_RET_OK;
}
