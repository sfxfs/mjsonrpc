/**
 * @file mjsonrpc.h
 * @brief A lightweight JSON-RPC 2.0 message parser and generator based on cJSON
 * @author Xiao
 * @date 2024
 * @version 1.0
 * 
 * @details
 * This library provides a complete implementation of JSON-RPC 2.0 specification
 * with minimal dependencies. It supports:
 * - JSON-RPC 2.0 request/response parsing and generation
 * - Batch requests (JSON Array)
 * - Notification requests
 * - Custom error handling
 * - Hash-based method indexing for performance
 * 
 * The library is designed to be integrated into various communication methods
 * (TCP, UDP, message queues, etc.) and provides a simple API for easy usage.
 *
 * @copyright
 * MIT License
 *
 * Copyright (c) 2024 Xiao
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MJSONRPC_H_
#define MJSONRPC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"
#include <stdint.h>

/**
 * @defgroup json_rpc_errors JSON-RPC 2.0 Standard Error Codes
 * @brief Standard error codes defined by JSON-RPC 2.0 specification
 * @{
 */

/** @brief Parse error - Invalid JSON was received by the server */
#define JSON_RPC_CODE_PARSE_ERROR (-32700)

/** @brief Invalid Request - The JSON sent is not a valid Request object */
#define JSON_RPC_CODE_INVALID_REQUEST (-32600)

/** @brief Method not found - The method does not exist / is not available */
#define JSON_RPC_CODE_METHOD_NOT_FOUND (-32601)

/** @brief Invalid params - Invalid method parameter(s) */
#define JSON_RPC_CODE_INVALID_PARAMS (-32602)

/** @brief Internal error - Internal JSON-RPC error */
#define JSON_RPC_CODE_INTERNAL_ERROR (-32603)

/** @brief Reserved for implementation-defined server-errors (-32000 to -32099) */
// -32000 to -32099 Reserved for implementation-defined server-errors.

/** @} */

/**
 * @enum mjrpc_error_return
 * @brief Return codes for mjsonrpc library functions
 */
enum mjrpc_error_return
{
    /** @brief Operation completed successfully */
    MJRPC_RET_OK,
    
    /** @brief Operation completed successfully (notification request) */
    MJRPC_RET_OK_NOTIFICATION,
    
    /** @brief Memory allocation failed */
    MJRPC_RET_ERROR_MEM_ALLOC_FAILED,
    
    /** @brief Requested method not found */
    MJRPC_RET_ERROR_NOT_FOUND,
    
    /** @brief Empty request received */
    MJRPC_RET_ERROR_EMPTY_REQUEST,
    
    /** @brief Request is not a JSON object or array */
    MJRPC_RET_ERROR_NOT_OBJ_ARY,
    
    /** @brief JSON parsing failed */
    MJRPC_RET_ERROR_PARSE_FAILED,
    
    /** @brief Handle not initialized */
    MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED,
    
    /** @brief Invalid parameter provided */
    MJRPC_RET_ERROR_INVALID_PARAM
};

/**
 * @struct mjrpc_func_ctx_t
 * @brief Context structure passed to RPC method callback functions
 * 
 * This structure provides context information and error handling capabilities
 * to RPC method implementations.
 */
typedef struct
{
    /** @brief User data pointer passed during method registration */
    void* data;
    
    /** @brief Error code to be set by the method implementation (0 = no error) */
    int32_t error_code;
    
    /** @brief Error message to be set by the method implementation (will be freed automatically) */
    char* error_message;
} mjrpc_func_ctx_t;

/**
 * @typedef mjrpc_func
 * @brief Function pointer type for RPC method implementations
 * 
 * @param context Context structure containing user data and error handling
 * @param params JSON parameters passed to the method (can be NULL)
 * @param id Request ID for responses (can be NULL for notifications)
 * @return cJSON* Result to be returned to client (will be freed automatically)
 * 
 * @note If error_code is set in context, the error_message will be used
 *       instead of the returned result
 */
typedef cJSON* (*mjrpc_func)(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id);

/**
 * @struct mjrpc_method
 * @brief Internal structure representing a registered RPC method
 * @internal
 */
struct mjrpc_method
{
    /** @brief Method name */
    char* name;
    
    /** @brief Function pointer to method implementation */
    mjrpc_func func;
    
    /** @brief User argument passed to the function */
    void* arg;
    
    /** @brief Internal state for hash table management */
    int state;
};

/**
 * @struct mjrpc_handle
 * @brief Main handle structure for managing RPC methods
 * 
 * This structure maintains a hash table of registered RPC methods
 * and provides efficient method lookup.
 */
typedef struct mjrpc_handle
{
    /** @brief Array of registered methods (hash table) */
    struct mjrpc_method* methods;
    
    /** @brief Total capacity of the hash table */
    size_t capacity;
    
    /** @brief Current number of registered methods */
    size_t size;
} mjrpc_handle_t;

/** @typedef mjrpc_handle_t
 * @brief Typedef for struct mjrpc_handle for convenience */


/**
 * @defgroup memory_hooks Memory Management Hooks
 * @brief Custom memory management function hooks
 * @{
 */

/**
 * @typedef mjrpc_malloc_func
 * @brief Function pointer type for custom memory allocation
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 * 
 * @note Should behave like standard malloc() function
 */
typedef void* (*mjrpc_malloc_func)(size_t size);

/**
 * @typedef mjrpc_free_func
 * @brief Function pointer type for custom memory deallocation
 * 
 * @param ptr Pointer to memory to free (can be NULL)
 * 
 * @note Should behave like standard free() function
 */
typedef void (*mjrpc_free_func)(void* ptr);

/**
 * @typedef mjrpc_strdup_func
 * @brief Function pointer type for custom string duplication
 * 
 * @param str String to duplicate
 * @return Pointer to newly allocated duplicate string, or NULL on failure
 * 
 * @note Should behave like standard strdup() function
 */
typedef char* (*mjrpc_strdup_func)(const char* str);

/**
 * @brief Set custom memory management functions
 * 
 * This function allows users to override the default memory management
 * functions used by the mjsonrpc library. All parameters must be provided
 * together, or pass NULL for all to reset to default standard library functions.
 * 
 * @param malloc_func Custom malloc function (required if not resetting)
 * @param free_func Custom free function (required if not resetting)
 * @param strdup_func Custom strdup function (required if not resetting)
 * 
 * @return MJRPC_RET_OK on success, MJRPC_RET_ERROR_INVALID_PARAM on invalid parameters
 * 
 * @note Call this function before creating any mjrpc_handle or processing requests
 * @note To reset to default functions, pass NULL for all parameters
 * @note These hooks only affect memory allocations made by mjsonrpc internally
 *       (handle, method names, error messages). They do NOT affect cJSON's own
 *       allocations. Strings returned by mjrpc_process_str() are allocated by
 *       cJSON (via cJSON_PrintUnformatted) and must be freed with standard free().
 * 
 * @par Example:
 * @code
 * // Set custom memory functions
 * mjrpc_set_memory_hooks(my_malloc, my_free, my_strdup);
 * 
 * // Reset to default functions
 * mjrpc_set_memory_hooks(NULL, NULL, NULL);
 * @endcode
 */
int mjrpc_set_memory_hooks(mjrpc_malloc_func malloc_func, mjrpc_free_func free_func,
                           mjrpc_strdup_func strdup_func);

/** @} */

/**
 * @defgroup client_api Client API
 * @brief Functions for creating JSON-RPC requests (client side)
 * @{
 */

/**
 * @brief Build a JSON-RPC request as a string
 * 
 * Creates a JSON-RPC 2.0 request string with the specified method, parameters,
 * and request ID. The parameters and ID will be released (freed) automatically.
 * 
 * @param method Name of the RPC method to call (must not be NULL)
 * @param params Parameters for the method (can be NULL, will be released)
 * @param id Request ID (can be NULL for notification, will be released)
 * 
 * @return String pointer containing the JSON-RPC request (caller must free)
 * @retval NULL If an error occurred (params and id will still be released)
 * 
 * @note For notification requests, pass NULL as the id parameter
 * 
 * @par Example:
 * @code
 * cJSON *params = cJSON_CreateObject();
 * cJSON_AddStringToObject(params, "name", "world");
 * cJSON *id = cJSON_CreateNumber(1);
 * char *request = mjrpc_request_str("hello", params, id);
 * if (request) {
 *     // Send request...
 *     free(request);
 * }
 * @endcode
 */
char* mjrpc_request_str(const char* method, cJSON* params, cJSON* id);

/**
 * @brief Build a JSON-RPC request as a cJSON object
 * 
 * Creates a JSON-RPC 2.0 request as a cJSON object with the specified method,
 * parameters, and request ID. The parameters and ID will be owned by the request.
 * 
 * @param method Name of the RPC method to call (must not be NULL)
 * @param params Parameters for the method (can be NULL, will be owned by request)
 * @param id Request ID (can be NULL for notification, will be owned by request)
 * 
 * @return cJSON pointer containing the JSON-RPC request (caller must delete)
 * @retval NULL If an error occurred (params and id will still be released)
 * 
 * @note For notification requests, pass NULL as the id parameter
 * 
 * @par Example:
 * @code
 * cJSON *params = cJSON_CreateArray();
 * cJSON_AddItemToArray(params, cJSON_CreateString("arg1"));
 * cJSON *id = cJSON_CreateString("req-123");
 * cJSON *request = mjrpc_request_cjson("method", params, id);
 * if (request) {
 *     // Use request...
 *     cJSON_Delete(request);
 * }
 * @endcode
 */
cJSON* mjrpc_request_cjson(const char* method, cJSON* params, cJSON* id);

/** @} */


/**
 * @defgroup server_api Server API
 * @brief Functions for handling JSON-RPC requests and managing methods (server side)
 * @{
 */

/**
 * @defgroup response_helpers Response Helper Functions
 * @brief Functions for creating JSON-RPC responses
 * @{
 */

/**
 * @brief Build a successful JSON-RPC response with result
 * 
 * Creates a JSON-RPC 2.0 response object containing the specified result
 * and request ID.
 * 
 * @param result Result data to be returned (will be owned by response)
 * @param id Client request ID (will be owned by response)
 * 
 * @return cJSON pointer containing the response (caller must delete)
 * @retval NULL If an error occurred (result and id will be released)
 * 
 * @note Both result and id must not be NULL
 * 
 * @par Example:
 * @code
 * cJSON *result = cJSON_CreateString("Hello, World!");
 * cJSON *id = cJSON_CreateNumber(1);
 * cJSON *response = mjrpc_response_ok(result, id);
 * if (response) {
 *     char *response_str = cJSON_Print(response);
 *     // Send response...
 *     free(response_str);
 *     cJSON_Delete(response);
 * }
 * @endcode
 */
cJSON* mjrpc_response_ok(cJSON* result, cJSON* id);

/**
 * @brief Build an error JSON-RPC response
 * 
 * Creates a JSON-RPC 2.0 error response object with the specified error
 * code, message, and request ID.
 * 
 * @param code Error code (standard JSON-RPC codes or custom codes)
 * @param message Error message (will be freed automatically, can be NULL)
 * @param id Client request ID (will be owned by response)
 * 
 * @return cJSON pointer containing the error response (caller must delete)
 * @retval NULL If an error occurred (message and id will be released)
 * 
 * @note If message is NULL, a default message will be used
 * 
 * @par Example:
 * @code
 * cJSON *id = cJSON_CreateNumber(1);
 * char *msg = strdup("Invalid parameters");
 * cJSON *response = mjrpc_response_error(JSON_RPC_CODE_INVALID_PARAMS, msg, id);
 * if (response) {
 *     // Send error response...
 *     cJSON_Delete(response);
 * }
 * @endcode
 */
cJSON* mjrpc_response_error(int code, char* message, cJSON* id);

/** @} */

/**
 * @defgroup handle_management Handle Management Functions
 * @brief Functions for creating and managing RPC handles
 * @{
 */

/**
 * @brief Create a new JSON-RPC handle
 * 
 * Allocates and initializes a new mjrpc_handle_t structure with the specified
 * initial capacity for the internal hash table.
 * 
 * @param initial_capacity Initial capacity of the hash table (0 for default)
 * 
 * @return Pointer to the created handle (caller must destroy)
 * @retval NULL If memory allocation failed
 * 
 * @note If initial_capacity is 0, a default capacity will be used
 * @note The handle must be destroyed with mjrpc_destroy_handle() to prevent memory leaks
 * 
 * @par Example:
 * @code
 * mjrpc_handle_t *handle = mjrpc_create_handle(32);
 * if (handle) {
 *     // Use handle...
 *     mjrpc_destroy_handle(handle);
 * }
 * @endcode
 */
mjrpc_handle_t* mjrpc_create_handle(size_t initial_capacity);

/**
 * @brief Destroy a JSON-RPC handle and free all associated memory
 * 
 * Destroys the specified handle, freeing all registered method names,
 * arguments, and the handle structure itself.
 * 
 * @param handle Handle to be destroyed (can be NULL)
 * 
 * @return Error code from enum mjrpc_error_return
 * @retval MJRPC_RET_OK If successful
 * @retval MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED If handle is NULL
 * 
 * @par Example:
 * @code
 * mjrpc_handle_t *handle = mjrpc_create_handle(0);
 * // ... use handle ...
 * int ret = mjrpc_destroy_handle(handle);
 * if (ret != MJRPC_RET_OK) {
 *     // Handle error...
 * }
 * @endcode
 */
int mjrpc_destroy_handle(mjrpc_handle_t* handle);

/** @} */

/**
 * @defgroup method_management Method Management Functions
 * @brief Functions for registering and unregistering RPC methods
 * @{
 */

/**
 * @brief Register a new RPC method
 * 
 * Adds a new method to the JSON-RPC handle with the specified callback
 * function, method name, and optional user argument.
 * 
 * @param handle JSON-RPC handle (must not be NULL)
 * @param function_pointer Callback function for the method (must not be NULL)
 * @param method_name Name of the method (must not be NULL)
 * @param arg2func User argument passed to the callback function (can be NULL).
 *                  If not NULL, it must be heap-allocated (via malloc/calloc),
 *                  as it will be automatically freed when the method is deleted
 *                  or the handle is destroyed.
 * 
 * @return Error code from enum mjrpc_error_return
 * @retval MJRPC_RET_OK If successful
 * @retval MJRPC_RET_ERROR_HANDLE_NOT_INITIALIZED If handle is NULL
 * @retval MJRPC_RET_ERROR_INVALID_PARAM If function_pointer or method_name is NULL
 * @retval MJRPC_RET_ERROR_MEM_ALLOC_FAILED If memory allocation failed
 * 
 * @note If a method with the same name already exists, it will be replaced
 * @note The method_name will be copied internally
 * 
 * @par Example:
 * @code
 * cJSON *hello_method(mjrpc_func_ctx_t *ctx, cJSON *params, cJSON *id) {
 *     return cJSON_CreateString("Hello, World!");
 * }
 * 
 * mjrpc_handle_t *handle = mjrpc_create_handle(0);
 * int ret = mjrpc_add_method(handle, hello_method, "hello", NULL);
 * if (ret != MJRPC_RET_OK) {
 *     // Handle error...
 * }
 * @endcode
 */
int mjrpc_add_method(mjrpc_handle_t* handle, mjrpc_func function_pointer, const char* method_name,
                     void* arg2func);

/**
 * @brief Unregister an RPC method
 * 
 * Removes the specified method from the JSON-RPC handle and frees
 * associated memory.
 * 
 * @param handle JSON-RPC handle (must not be NULL)
 * @param method_name Name of the method to remove (must not be NULL)
 * 
 * @return Error code from enum mjrpc_error_return
 * @retval MJRPC_RET_OK If successful
 * @retval MJRPC_RET_ERROR_INVALID_PARAM If method_name is NULL
 * @retval MJRPC_RET_ERROR_NOT_FOUND If method was not found
 * 
 * @par Example:
 * @code
 * int ret = mjrpc_del_method(handle, "hello");
 * if (ret == MJRPC_RET_ERROR_NOT_FOUND) {
 *     printf("Method 'hello' not found\n");
 * }
 * @endcode
 */
int mjrpc_del_method(mjrpc_handle_t* handle, const char* method_name);

/** @} */

/**
 * @defgroup request_processing Request Processing Functions
 * @brief Functions for processing JSON-RPC requests
 * @{
 */

/**
 * @brief Process a JSON-RPC request string
 * 
 * Parses and processes a JSON-RPC request string, calling the appropriate
 * registered method and returning the response as a string.
 * 
 * @param handle JSON-RPC handle containing registered methods
 * @param request_str JSON-RPC request string (must be valid JSON)
 * @param ret_code Pointer to store the return code (can be NULL)
 * 
 * @return Response string (caller must free), or NULL for notifications
 * @retval NULL If the request was a notification or an error occurred
 * 
 * @note The returned string must be freed by the caller
 * @note Supports both single requests and batch requests (JSON arrays)
 * 
 * @par Example:
 * @code
 * const char *request = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}";
 * int ret_code;
 * char *response = mjrpc_process_str(handle, request, &ret_code);
 * if (response) {
 *     printf("Response: %s\n", response);
 *     free(response);
 * } else if (ret_code == MJRPC_RET_OK_NOTIFICATION) {
 *     printf("Notification processed\n");
 * }
 * @endcode
 */
char* mjrpc_process_str(mjrpc_handle_t* handle, const char* request_str, int* ret_code);

/**
 * @brief Process a JSON-RPC request cJSON object
 * 
 * Processes a JSON-RPC request cJSON object, calling the appropriate
 * registered method and returning the response as a cJSON object.
 * 
 * @param handle JSON-RPC handle containing registered methods
 * @param request_cjson JSON-RPC request cJSON object
 * @param ret_code Pointer to store the return code (can be NULL)
 * 
 * @return Response cJSON object (caller must delete), or NULL for notifications
 * @retval NULL If the request was a notification or an error occurred
 * 
 * @note The returned cJSON object must be deleted by the caller
 * @note Supports both single requests and batch requests (JSON arrays)
 * 
 * @par Example:
 * @code
 * cJSON *request = cJSON_Parse("{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"id\":1}");
 * int ret_code;
 * cJSON *response = mjrpc_process_cjson(handle, request, &ret_code);
 * if (response) {
 *     char *response_str = cJSON_Print(response);
 *     printf("Response: %s\n", response_str);
 *     free(response_str);
 *     cJSON_Delete(response);
 * }
 * cJSON_Delete(request);
 * @endcode
 */
cJSON* mjrpc_process_cjson(mjrpc_handle_t* handle, const cJSON* request_cjson, int* ret_code);

/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif // MJSONRPC_H_
