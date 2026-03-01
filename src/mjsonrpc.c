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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
  size_t hash_value = 5381;  /* Initial hash value (prime) */
  while (*key) {
    hash_value = ((hash_value << 5) + hash_value) + (unsigned char)(*key++);
  }
  return hash_value % capacity;
}

/**
 * @brief Compute second hash value for double hashing
 * @param key String key to hash
 * @param capacity Hash table capacity
 * @return Second hash value for probe step size
 *
 * @note Returns a prime-based hash for use as step size in double hashing
 *       Ensures step size is relatively prime to table capacity
 */
static size_t hash2(const char *key, size_t capacity) {
  if (key == NULL)
    return 1;  /* Minimum step size */
  size_t hash_value = 0;
  while (*key) {
    hash_value = (hash_value * HASH_MULTIPLIER2) + (unsigned char)(*key++);
  }
  /* Return value in range [1, capacity-1] to ensure valid probe step */
  return 1 + (hash_value % (capacity - 1));
}

static int resize(mjrpc_handle_t *handle) {
  init_memory_hooks_if_needed();
  const size_t old_capacity = handle->capacity;
  struct mjrpc_method *old_methods = handle->methods;
  int rehash_failures = 0;

  /* Check for potential overflow */
  if (old_capacity > SIZE_MAX / 2) {
    log_error("Hash table resize overflow", 0);
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
  }

  handle->capacity *= 2;
  handle->methods = (struct mjrpc_method *)g_mjrpc_malloc(
      handle->capacity * sizeof(struct mjrpc_method));
  if (handle->methods == NULL) {
    handle->capacity = old_capacity;
    handle->methods = old_methods;
    log_error("Hash table resize memory allocation failed", MJRPC_RET_ERROR_MEM_ALLOC_FAILED);
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
  }
  memset(handle->methods, 0, handle->capacity * sizeof(struct mjrpc_method));
  handle->size = 0;

  for (size_t i = 0; i < old_capacity; i++) {
    if (old_methods[i].state == OCCUPIED) {
      /* Store name pointer before passing ownership */
      char *name_copy = old_methods[i].name;
      void *arg_copy = old_methods[i].arg;
      mjrpc_func func_copy = old_methods[i].func;

      int add_result = mjrpc_add_method(handle, func_copy, name_copy, arg_copy);
      if (add_result != MJRPC_RET_OK) {
        /* Failed to add method during resize, free the resources */
        g_mjrpc_free(name_copy);
        g_mjrpc_free(arg_copy);
        rehash_failures++;
        log_error("Method rehash failed during resize", add_result);
        /* Continue with remaining methods instead of aborting */
        continue;
      }
      /* name_copy and arg_copy now owned by new entry, don't free them */
    }
  }
  g_mjrpc_free(old_methods);

  if (rehash_failures > 0) {
    log_error("Hash table resize completed with method failures", rehash_failures);
  }
  return MJRPC_RET_OK;
}

static bool method_get(const mjrpc_handle_t *handle, const char *key,
                       mjrpc_func *func, void **arg) {
  if (handle == NULL || key == NULL || func == NULL || arg == NULL) {
    return false;
  }

  size_t index = hash(key, handle->capacity);
  size_t step_size = 0;
  size_t probe_count = 0;

  /* Use double hashing for better distribution with high load factors */
  while (handle->methods[index].state != EMPTY) {
    if (handle->methods[index].state == OCCUPIED &&
        strcmp(handle->methods[index].name, key) == 0) {
      *func = handle->methods[index].func;
      *arg = handle->methods[index].arg;
      return true;
    }
    probe_count++;
    if (probe_count >= handle->capacity) {
      break;  /* Table is full, key not found */
    }
    /* Double hashing: index = (hash1 + i * hash2) % capacity */
    if (step_size == 0) {
      step_size = hash2(key, handle->capacity);  /* Compute step size on first probe */
    }
    index = (index + step_size) % handle->capacity;
  }
  return false;
}

/*--- private functions ---*/

static cJSON *invoke_callback(const mjrpc_handle_t *handle,
                              const char *method_name, cJSON *params, cJSON *id,
                              int params_type) {
  init_memory_hooks_if_needed();
  cJSON *returned = NULL;
  mjrpc_func func = NULL;
  void *arg = NULL;
  mjrpc_func_ctx_t ctx = {0};
  ctx.error_code = 0;
  ctx.error_message = NULL;
  ctx.params_type = params_type;
  if (!method_get((mjrpc_handle_t *)handle, method_name, &func, &arg) ||
      !func) {
    return mjrpc_response_error(JSON_RPC_CODE_METHOD_NOT_FOUND,
                                g_mjrpc_strdup("Method not found."), id);
  }
  ctx.data = arg;
  returned = func(&ctx, params, id);
  if (ctx.error_code) {
    cJSON_Delete(returned);
    return mjrpc_response_error(ctx.error_code, ctx.error_message, id);
  }
  return mjrpc_response_ok(returned, id);
}

static cJSON *rpc_handle_obj_req(const mjrpc_handle_t *handle,
                                 const cJSON *request) {
  init_memory_hooks_if_needed();
  cJSON *id = cJSON_GetObjectItem(request, "id");

#ifdef cJSON_Int
  if (id == NULL || id->type == cJSON_NULL || id->type == cJSON_String ||
      id->type == cJSON_Int)
#else
  if (id == NULL || id->type == cJSON_NULL || id->type == cJSON_String ||
      id->type == cJSON_Number)
#endif
  {
    cJSON *id_copy = NULL;
    if (id) {
      if (id->type == cJSON_NULL)
        id_copy = cJSON_CreateNull();
      else
        id_copy = (id->type == cJSON_String)
                      ? cJSON_CreateString(id->valuestring)
                      : cJSON_CreateNumber(id->valuedouble);
    }
    const cJSON *version = cJSON_GetObjectItem(request, "jsonrpc");
    if (version == NULL || version->type != cJSON_String ||
        strcmp("2.0", version->valuestring) != 0)
      return mjrpc_response_error(
          JSON_RPC_CODE_INVALID_REQUEST,
          g_mjrpc_strdup("Invalid request received: JSONRPC version error."),
          id_copy);

    const cJSON *method = cJSON_GetObjectItem(request, "method");
    if (method != NULL && method->type == cJSON_String) {
      cJSON *params = cJSON_GetObjectItem(request, "params");

      // Determine params type: 0=object, 1=array, 2=no params
      int actual_params_type = 2; // no params by default
      if (params != NULL) {
        actual_params_type = (params->type == cJSON_Array) ? 1 : 0;
      }

      return invoke_callback(handle, method->valuestring, params, id_copy,
                             actual_params_type);
    }
    return mjrpc_response_error(
        JSON_RPC_CODE_INVALID_REQUEST,
        g_mjrpc_strdup("Invalid request received: No 'method' member."),
        id_copy);
  }
  // Invalid id type
  return mjrpc_response_error(
      JSON_RPC_CODE_INVALID_REQUEST,
      g_mjrpc_strdup("Invalid request received: 'id' member type error."),
      cJSON_CreateNull());
}

static cJSON *rpc_handle_ary_req(const mjrpc_handle_t *handle,
                                 const cJSON *request, const int array_size) {
  int valid_reqs = 0;
  cJSON *return_json_array = cJSON_CreateArray();
  for (int i = 0; i < array_size; i++) {
    cJSON *obj_req = rpc_handle_obj_req(handle, cJSON_GetArrayItem(request, i));
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

  cJSON_AddStringToObject(json, "jsonrpc", "2.0");
  cJSON_AddStringToObject(json, "method", method);
  cJSON_AddItemToObject(json, "params", params);
  cJSON_AddItemToObject(json, "id", id);

  return json;
}

char *mjrpc_request_str(const char *method, cJSON *params, cJSON *id) {
  cJSON *json = mjrpc_request_cjson(method, params, id);
  if (json == NULL)
    return NULL;

  char *json_str = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);

  // Note: json_str must be freed by caller using standard free()
  // (cJSON uses malloc internally)
  return json_str;
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

  cJSON_AddStringToObject(result_root, "jsonrpc", "2.0");
  cJSON_AddItemToObject(result_root, "result", result);
  cJSON_AddItemToObject(result_root, "id", id);

  return result_root;
}

cJSON *mjrpc_response_error(int code, const char *message, cJSON *id) {
  init_memory_hooks_if_needed();

  cJSON *result_root = cJSON_CreateObject();
  cJSON *error_root = cJSON_CreateObject();
  if (result_root == NULL || error_root == NULL || id == NULL) {
    if (message)
      g_mjrpc_free(
          (void *)message); // Free the message string if it was allocated
    cJSON_Delete(id);
    cJSON_Delete(error_root);
    cJSON_Delete(result_root);
    return NULL;
  }

  cJSON_AddNumberToObject(error_root, "code", code);
  if (message) {
    cJSON_AddStringToObject(error_root, "message", message);
    g_mjrpc_free(
        (void *)message); // Free the message string after adding to JSON
  } else
    cJSON_AddStringToObject(error_root, "message", "No message here.");

  cJSON_AddStringToObject(result_root, "jsonrpc", "2.0");
  cJSON_AddItemToObject(result_root, "error", error_root);
  cJSON_AddItemToObject(result_root, "id", id);

  return result_root;
}

mjrpc_handle_t *mjrpc_create_handle(size_t initial_capacity) {
  init_memory_hooks_if_needed();
  if (initial_capacity == 0)
    initial_capacity = DEFAULT_INITIAL_CAPACITY;
  mjrpc_handle_t *handle = g_mjrpc_malloc(sizeof(mjrpc_handle_t));
  if (handle == NULL)
    return NULL;
  handle->capacity = initial_capacity;
  handle->size = 0;
  handle->methods = (struct mjrpc_method *)g_mjrpc_malloc(
      handle->capacity * sizeof(struct mjrpc_method));
  if (handle->methods == NULL) {
    g_mjrpc_free(handle);
    return NULL;
  }
  memset(handle->methods, 0, handle->capacity * sizeof(struct mjrpc_method));
  return handle;
}

int mjrpc_destroy_handle(mjrpc_handle_t *handle) {
  init_memory_hooks_if_needed();
  if (handle == NULL)
    return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
  for (size_t i = 0; i < handle->capacity; i++) {
    if (handle->methods[i].state == OCCUPIED) {
      g_mjrpc_free(handle->methods[i].name);
      if (handle->methods[i].arg != NULL)
        g_mjrpc_free(handle->methods[i].arg);
    }
  }
  g_mjrpc_free(handle->methods);
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

  size_t index = hash(method_name, handle->capacity);
  size_t step_size = 0;
  size_t probe_count = 0;

  /* Use double hashing for better distribution with high load factors */
  while (handle->methods[index].state != EMPTY &&
         handle->methods[index].state != DELETED) {
    if (strcmp(handle->methods[index].name, method_name) == 0) {
      /* Method already exists, update it and free old arg if exists */
      if (handle->methods[index].arg != NULL) {
        g_mjrpc_free(handle->methods[index].arg);
      }
      handle->methods[index].func = function_pointer;
      handle->methods[index].arg = arg2func;
      return MJRPC_RET_OK;
    }
    probe_count++;
    if (probe_count >= handle->capacity) {
      break;  /* Table is full, should not happen with resize */
    }
    /* Double hashing: index = (hash1 + i * hash2) % capacity */
    if (step_size == 0) {
      step_size = hash2(method_name, handle->capacity);  /* Compute step size on first probe */
    }
    index = (index + step_size) % handle->capacity;
  }

  /* Check if hash table is full (shouldn't happen with resize, but safety check) */
  if (probe_count >= handle->capacity) {
    log_error("Hash table full during add_method (should not happen)", MJRPC_RET_ERROR_MEM_ALLOC_FAILED);
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
  }

  handle->methods[index].name = g_mjrpc_strdup(method_name);
  if (handle->methods[index].name == NULL) {
    log_error("strdup failed during add_method", MJRPC_RET_ERROR_MEM_ALLOC_FAILED);
    return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
  }
  handle->methods[index].func = function_pointer;
  handle->methods[index].arg = arg2func;
  handle->methods[index].state = OCCUPIED;
  handle->size++;
  return MJRPC_RET_OK;
}

int mjrpc_del_method(mjrpc_handle_t *handle, const char *name) {
  init_memory_hooks_if_needed();
  if (handle == NULL)
    return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
  if (name == NULL)
    return MJRPC_RET_ERROR_INVALID_PARAM;

  size_t index = hash(name, handle->capacity);
  size_t step_size = 0;
  size_t probe_count = 0;

  /* Use double hashing for better distribution with high load factors */
  while (handle->methods[index].state != EMPTY) {
    if (handle->methods[index].state == OCCUPIED &&
        strcmp(handle->methods[index].name, name) == 0) {
      g_mjrpc_free(handle->methods[index].name);
      handle->methods[index].name = NULL;
      if (handle->methods[index].arg != NULL) {
        g_mjrpc_free(handle->methods[index].arg);
        handle->methods[index].arg = NULL;
      }
      handle->methods[index].state = DELETED;
      handle->size--;
      return MJRPC_RET_OK;
    }
    probe_count++;
    if (probe_count >= handle->capacity) {
      break;  /* Table is full, key not found */
    }
    /* Double hashing: index = (hash1 + i * hash2) % capacity */
    if (step_size == 0) {
      step_size = hash2(name, handle->capacity);  /* Compute step size on first probe */
    }
    index = (index + step_size) % handle->capacity;
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

  for (size_t i = 0; i < handle->capacity; i++) {
    if (handle->methods[i].state == OCCUPIED) {
      callback(handle->methods[i].name, handle->methods[i].arg, user_data);
    }
  }
  return MJRPC_RET_OK;
}

char *mjrpc_process_str(const mjrpc_handle_t *handle, const char *request_str,
                        int *ret_code) {
  cJSON *request = cJSON_Parse(request_str);
  if (request == NULL) {
    // Parse failed, create error response
    if (ret_code) {
      *ret_code = MJRPC_RET_ERROR_PARSE_FAILED;
    }
    cJSON *error_resp = mjrpc_response_error(
        JSON_RPC_CODE_PARSE_ERROR,
        g_mjrpc_strdup(
            "Invalid request received: Not a JSON formatted request."),
        cJSON_CreateNull());
    if (error_resp) {
      char *response_str = cJSON_PrintUnformatted(error_resp);
      cJSON_Delete(error_resp);
      return response_str;
    }
    return NULL;
  }

  cJSON *response = mjrpc_process_cjson(handle, request, ret_code);
  cJSON_Delete(request);

  if (response) {
    char *response_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return response_str;
  }
  return NULL;
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
        g_mjrpc_strdup(
            "Invalid request received: Not a JSON formatted request."),
        cJSON_CreateNull());
  }

  cJSON *cjson_return = NULL;
  if (request_cjson->type == cJSON_Array) {
    int array_size = cJSON_GetArraySize(request_cjson);
    if (array_size <= 0) {
      ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
      cjson_return = mjrpc_response_error(
          JSON_RPC_CODE_PARSE_ERROR,
          g_mjrpc_strdup("Invalid request received: Empty JSON array."),
          cJSON_CreateNull());
    } else {
      cjson_return = rpc_handle_ary_req(handle, request_cjson, array_size);
      if (cjson_return)
        ret = MJRPC_RET_OK;
      else
        ret = MJRPC_RET_OK_NOTIFICATION;
    }
  } else if (request_cjson->type == cJSON_Object) {
    // Check if the object is empty by checking if it has any children
    const int obj_size = (request_cjson->child != NULL) ? 1 : 0;
    if (obj_size <= 0) {
      ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
      cjson_return = mjrpc_response_error(
          JSON_RPC_CODE_PARSE_ERROR,
          g_mjrpc_strdup("Invalid request received: Empty JSON object."),
          cJSON_CreateNull());
    } else {
      cjson_return = rpc_handle_obj_req(handle, request_cjson);
      if (cjson_return)
        ret = MJRPC_RET_OK;
      else
        ret = MJRPC_RET_OK_NOTIFICATION;
    }
  } else {
    cjson_return = mjrpc_response_error(
        JSON_RPC_CODE_PARSE_ERROR,
        g_mjrpc_strdup("Invalid request received: Not a JSON object or array."),
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
