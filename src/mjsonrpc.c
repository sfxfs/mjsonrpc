/*
    MIT License

    Copyright (c) 2024 Xiao

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "mjsonrpc.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*--- memory management hooks ---*/

// Thread-local storage for memory function pointers, default to standard library functions
static _Thread_local mjrpc_malloc_func g_mjrpc_malloc = NULL;
static _Thread_local mjrpc_free_func g_mjrpc_free = NULL;
static _Thread_local mjrpc_strdup_func g_mjrpc_strdup = NULL;

static inline void init_memory_hooks_if_needed(void)
{
    if (g_mjrpc_malloc == NULL)
        g_mjrpc_malloc = malloc;
    if (g_mjrpc_free == NULL)
        g_mjrpc_free = free;
    if (g_mjrpc_strdup == NULL)
        g_mjrpc_strdup = strdup;
}

/*--- utility ---*/

enum method_state
{
    EMPTY,
    OCCUPIED,
    DELETED
};

static unsigned int hash(const char* key, size_t capacity)
{
    unsigned int hash_value = 0;
    while (*key)
    {
        hash_value = (hash_value * 31) + (*key++);
    }
    return hash_value % capacity;
}

static int resize(mjrpc_handle_t* handle)
{
    init_memory_hooks_if_needed();
    const size_t old_capacity = handle->capacity;
    struct mjrpc_method* old_methods = handle->methods;

    handle->capacity *= 2;
    handle->methods =
        (struct mjrpc_method*) g_mjrpc_malloc(handle->capacity * sizeof(struct mjrpc_method));
    if (handle->methods == NULL)
    {
        handle->capacity = old_capacity;
        handle->methods = old_methods;
        return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;
    }
    memset(handle->methods, 0, handle->capacity * sizeof(struct mjrpc_method));
    handle->size = 0;

    for (size_t i = 0; i < old_capacity; i++)
    {
        if (old_methods[i].state == OCCUPIED)
        {
            mjrpc_add_method(handle, old_methods[i].func, old_methods[i].name, old_methods[i].arg);
            g_mjrpc_free(old_methods[i].name);
        }
    }
    g_mjrpc_free(old_methods);
    return MJRPC_RET_OK;
}

static bool method_get(const mjrpc_handle_t* handle, const char* key, mjrpc_func* func, void** arg)
{
    unsigned int index = hash(key, handle->capacity);
    size_t probe_count = 0;

    while (handle->methods[index].state != EMPTY && probe_count < handle->capacity)
    {
        if (handle->methods[index].state == OCCUPIED &&
            strcmp(handle->methods[index].name, key) == 0)
        {
            *func = handle->methods[index].func;
            *arg = handle->methods[index].arg;
            return true;
        }
        probe_count++;
        index = (index + probe_count * probe_count) % handle->capacity;
    }
    return false;
}

/*--- private functions ---*/

static cJSON* invoke_callback(const mjrpc_handle_t* handle, const char* method_name, cJSON* params,
                              cJSON* id, int params_type)
{
    init_memory_hooks_if_needed();
    cJSON* returned = NULL;
    mjrpc_func func = NULL;
    void* arg = NULL;
    mjrpc_func_ctx_t ctx = {0};
    ctx.error_code = 0;
    ctx.error_message = NULL;
    ctx.params_type = params_type;
    if (!method_get((mjrpc_handle_t*) handle, method_name, &func, &arg) || !func)
    {
        return mjrpc_response_error(JSON_RPC_CODE_METHOD_NOT_FOUND,
                                    g_mjrpc_strdup("Method not found."), id);
    }
    ctx.data = arg;
    returned = func(&ctx, params, id);
    if (ctx.error_code)
    {
        cJSON_Delete(returned);
        return mjrpc_response_error(ctx.error_code, ctx.error_message, id);
    }
    return mjrpc_response_ok(returned, id);
}

static cJSON* rpc_handle_obj_req(const mjrpc_handle_t* handle, const cJSON* request)
{
    init_memory_hooks_if_needed();
    cJSON* id = cJSON_GetObjectItem(request, "id");

#ifdef cJSON_Int
    if (id == NULL || id->type == cJSON_NULL || id->type == cJSON_String || id->type == cJSON_Int)
#else
    if (id == NULL || id->type == cJSON_NULL || id->type == cJSON_String ||
        id->type == cJSON_Number)
#endif
    {
        cJSON* id_copy = NULL;
        if (id)
        {
            if (id->type == cJSON_NULL)
                id_copy = cJSON_CreateNull();
            else
                id_copy = (id->type == cJSON_String) ? cJSON_CreateString(id->valuestring) :
                                                     cJSON_CreateNumber(id->valuedouble);
        }
        const cJSON* version = cJSON_GetObjectItem(request, "jsonrpc");
        if (version == NULL || version->type != cJSON_String ||
            strcmp("2.0", version->valuestring) != 0)
            return mjrpc_response_error(
                JSON_RPC_CODE_INVALID_REQUEST,
                g_mjrpc_strdup("Invalid request received: JSONRPC version error."), id_copy);

        const cJSON* method = cJSON_GetObjectItem(request, "method");
        if (method != NULL && method->type == cJSON_String)
        {
            cJSON* params = cJSON_GetObjectItem(request, "params");

            // Determine params type: 0=object, 1=array, 2=no params
            int actual_params_type = 2; // no params by default
            if (params != NULL)
            {
                actual_params_type = (params->type == cJSON_Array) ? 1 : 0;
            }

            return invoke_callback(handle, method->valuestring, params, id_copy, actual_params_type);
        }
        return mjrpc_response_error(JSON_RPC_CODE_INVALID_REQUEST,
                                    g_mjrpc_strdup("Invalid request received: No 'method' member."),
                                    id_copy);
    }
    // Invalid id type
    return mjrpc_response_error(JSON_RPC_CODE_INVALID_REQUEST,
                                g_mjrpc_strdup("Invalid request received: 'id' member type error."),
                                cJSON_CreateNull());
}

static cJSON* rpc_handle_ary_req(const mjrpc_handle_t* handle, const cJSON* request,
                                 const int array_size)
{
    int valid_reqs = 0;
    cJSON* return_json_array = cJSON_CreateArray();
    for (int i = 0; i < array_size; i++)
    {
        cJSON* obj_req = rpc_handle_obj_req(handle, cJSON_GetArrayItem(request, i));
        if (obj_req)
        {
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

cJSON* mjrpc_request_cjson(const char* method, cJSON* params, cJSON* id)
{
    if (method == NULL)
    {
        cJSON_Delete(params);
        cJSON_Delete(id);
        return NULL;
    }

    cJSON* json = cJSON_CreateObject();
    if (json == NULL)
    {
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

char* mjrpc_request_str(const char* method, cJSON* params, cJSON* id)
{
    cJSON* json = mjrpc_request_cjson(method, params, id);
    if (json == NULL)
        return NULL;

    char* json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    return json_str;
}

cJSON* mjrpc_response_ok(cJSON* result, cJSON* id)
{
    if (id == NULL || result == NULL)
    {
        cJSON_Delete(result);
        cJSON_Delete(id);
        return NULL;
    }

    cJSON* result_root = cJSON_CreateObject();
    if (result_root == NULL)
    {
        cJSON_Delete(result);
        cJSON_Delete(id);
        return NULL;
    }

    cJSON_AddStringToObject(result_root, "jsonrpc", "2.0");
    cJSON_AddItemToObject(result_root, "result", result);
    cJSON_AddItemToObject(result_root, "id", id);

    return result_root;
}

cJSON* mjrpc_response_error(int code, const char* message, cJSON* id)
{
    init_memory_hooks_if_needed();

    // Make a copy of message if provided, since we take ownership
    char* message_copy = NULL;
    if (message != NULL)
    {
        message_copy = g_mjrpc_strdup(message);
        if (message_copy == NULL)
        {
            cJSON_Delete(id);
            return NULL;
        }
        // Free the original message since caller no longer owns it
        g_mjrpc_free((void*) message);
    }

    if (id == NULL)
    {
        if (message_copy)
            g_mjrpc_free(message_copy);
        return NULL;
    }

    cJSON* result_root = cJSON_CreateObject();
    if (result_root == NULL)
    {
        if (message_copy)
            g_mjrpc_free(message_copy);
        cJSON_Delete(id);
        return NULL;
    }

    cJSON* error_root = cJSON_CreateObject();
    if (error_root == NULL)
    {
        if (message_copy)
            g_mjrpc_free(message_copy);
        cJSON_Delete(id);
        cJSON_Delete(result_root);
        return NULL;
    }

    cJSON_AddNumberToObject(error_root, "code", code);
    if (message_copy)
    {
        cJSON_AddStringToObject(error_root, "message", message_copy);
        g_mjrpc_free(message_copy);
    }
    else
    {
        cJSON_AddStringToObject(error_root, "message", "No message here.");
    }

    cJSON_AddStringToObject(result_root, "jsonrpc", "2.0");
    cJSON_AddItemToObject(result_root, "error", error_root);
    cJSON_AddItemToObject(result_root, "id", id);

    return result_root;
}

#define DEFAULT_INITIAL_CAPACITY 16

mjrpc_handle_t* mjrpc_create_handle(size_t initial_capacity)
{
    init_memory_hooks_if_needed();
    if (initial_capacity == 0)
        initial_capacity = DEFAULT_INITIAL_CAPACITY;
    mjrpc_handle_t* handle = g_mjrpc_malloc(sizeof(mjrpc_handle_t));
    if (handle == NULL)
        return NULL;
    handle->capacity = initial_capacity;
    handle->size = 0;
    handle->methods =
        (struct mjrpc_method*) g_mjrpc_malloc(handle->capacity * sizeof(struct mjrpc_method));
    if (handle->methods == NULL)
    {
        g_mjrpc_free(handle);
        return NULL;
    }
    memset(handle->methods, 0, handle->capacity * sizeof(struct mjrpc_method));
    return handle;
}

int mjrpc_destroy_handle(mjrpc_handle_t* handle)
{
    init_memory_hooks_if_needed();
    if (handle == NULL)
        return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
    for (size_t i = 0; i < handle->capacity; i++)
    {
        if (handle->methods[i].state == OCCUPIED)
        {
            g_mjrpc_free(handle->methods[i].name);
            if (handle->methods[i].arg != NULL)
                g_mjrpc_free(handle->methods[i].arg);
        }
    }
    g_mjrpc_free(handle->methods);
    g_mjrpc_free(handle);
    return MJRPC_RET_OK;
}

int mjrpc_add_method(mjrpc_handle_t* handle, mjrpc_func function_pointer, const char* method_name,
                     void* arg2func)
{
    init_memory_hooks_if_needed();
    if (handle == NULL)
        return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
    if (function_pointer == NULL || method_name == NULL)
        return MJRPC_RET_ERROR_INVALID_PARAM;

    if ((double) handle->size / (double) handle->capacity >= 0.75)
        if (resize(handle) != MJRPC_RET_OK)
            return MJRPC_RET_ERROR_MEM_ALLOC_FAILED;

    unsigned int index = hash(method_name, handle->capacity);
    size_t probe_count = 0;

    while (handle->methods[index].state != EMPTY && handle->methods[index].state != DELETED &&
           probe_count < handle->capacity)
    {
        if (strcmp(handle->methods[index].name, method_name) == 0)
        {
            handle->methods[index].func = function_pointer;
            handle->methods[index].arg = arg2func;
            return MJRPC_RET_OK;
        }
        probe_count++;
        index = (index + probe_count * probe_count) % handle->capacity;
    }

    handle->methods[index].name = g_mjrpc_strdup(method_name);
    handle->methods[index].func = function_pointer;
    handle->methods[index].arg = arg2func;
    handle->methods[index].state = OCCUPIED;
    handle->size++;
    return MJRPC_RET_OK;
}

int mjrpc_del_method(mjrpc_handle_t* handle, const char* name)
{
    init_memory_hooks_if_needed();
    if (handle == NULL)
        return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
    if (name == NULL)
        return MJRPC_RET_ERROR_INVALID_PARAM;
    unsigned int index = hash(name, handle->capacity);
    size_t probe_count = 0;

    while (handle->methods[index].state != EMPTY && probe_count < handle->capacity)
    {
        if (handle->methods[index].state == OCCUPIED &&
            strcmp(handle->methods[index].name, name) == 0)
        {
            g_mjrpc_free(handle->methods[index].name);
            if (handle->methods[index].arg != NULL)
                g_mjrpc_free(handle->methods[index].arg);
            handle->methods[index].state = DELETED;
            handle->size--;
            return MJRPC_RET_OK;
        }
        probe_count++;
        index = (index + probe_count * probe_count) % handle->capacity;
    }
    return MJRPC_RET_ERROR_NOT_FOUND;
}

size_t mjrpc_get_method_count(mjrpc_handle_t* handle)
{
    if (handle == NULL)
        return 0;
    return handle->size;
}

int mjrpc_enum_methods(mjrpc_handle_t* handle,
    void (*callback)(const char* method_name, void* arg, void* user_data),
    void* user_data)
{
    if (handle == NULL)
        return MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
    if (callback == NULL)
        return MJRPC_RET_ERROR_INVALID_PARAM;

    for (size_t i = 0; i < handle->capacity; i++)
    {
        if (handle->methods[i].state == OCCUPIED)
        {
            callback(handle->methods[i].name, handle->methods[i].arg, user_data);
        }
    }
    return MJRPC_RET_OK;
}

char* mjrpc_process_str(mjrpc_handle_t* handle, const char* request_str, int* ret_code)
{
    cJSON* request = cJSON_Parse(request_str);
    cJSON* response = mjrpc_process_cjson(handle, request, ret_code);
    cJSON_Delete(request);
    if (response)
    {
        char* response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        return response_str;
    }
    return NULL;
}

cJSON* mjrpc_process_cjson(mjrpc_handle_t* handle, const cJSON* request_cjson, int* ret_code)
{
    init_memory_hooks_if_needed();
    int ret = MJRPC_RET_OK;
    if (handle == NULL)
    {
        ret = MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED;
        if (ret_code)
            *ret_code = ret;
        return NULL;
    }

    if (request_cjson == NULL)
    {
        ret = MJRPC_RET_ERROR_PARSE_FAILED;
        if (ret_code)
            *ret_code = ret;
        return mjrpc_response_error(
            JSON_RPC_CODE_PARSE_ERROR,
            g_mjrpc_strdup("Invalid request received: Not a JSON formatted request."),
            cJSON_CreateNull());
    }

    cJSON* cjson_return = NULL;
    if (request_cjson->type == cJSON_Array)
    {
        int array_size = cJSON_GetArraySize(request_cjson);
        if (array_size <= 0)
        {
            ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
            cjson_return = mjrpc_response_error(
                JSON_RPC_CODE_PARSE_ERROR,
                g_mjrpc_strdup("Invalid request received: Empty JSON array."), cJSON_CreateNull());
        }
        else
        {
            cjson_return = rpc_handle_ary_req(handle, request_cjson, array_size);
            if (cjson_return)
                ret = MJRPC_RET_OK;
            else
                ret = MJRPC_RET_OK_NOTIFICATION;
        }
    }
    else if (request_cjson->type == cJSON_Object)
    {
        const int obj_size = cJSON_GetArraySize(request_cjson);
        if (obj_size <= 0)
        {
            ret = MJRPC_RET_ERROR_EMPTY_REQUEST;
            cjson_return = mjrpc_response_error(
                JSON_RPC_CODE_PARSE_ERROR,
                g_mjrpc_strdup("Invalid request received: Empty JSON object."), cJSON_CreateNull());
        }
        else
        {
            cjson_return = rpc_handle_obj_req(handle, request_cjson);
            if (cjson_return)
                ret = MJRPC_RET_OK;
            else
                ret = MJRPC_RET_OK_NOTIFICATION;
        }
    }
    else
    {
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

int mjrpc_set_memory_hooks(mjrpc_malloc_func malloc_func, mjrpc_free_func free_func,
                           mjrpc_strdup_func strdup_func)
{
    // Initialize defaults if not yet initialized
    init_memory_hooks_if_needed();

    // If all parameters are NULL, reset to default functions
    if (malloc_func == NULL && free_func == NULL && strdup_func == NULL)
    {
        g_mjrpc_malloc = malloc;
        g_mjrpc_free = free;
        g_mjrpc_strdup = strdup;
        return MJRPC_RET_OK;
    }

    // If any parameter is not NULL, all must be provided
    if (malloc_func == NULL || free_func == NULL || strdup_func == NULL)
    {
        return MJRPC_RET_ERROR_INVALID_PARAM;
    }

    // Set custom functions
    g_mjrpc_malloc = malloc_func;
    g_mjrpc_free = free_func;
    g_mjrpc_strdup = strdup_func;

    return MJRPC_RET_OK;
}
