#include "mjsonrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Statistics for tracking memory usage
static size_t malloc_count = 0;
static size_t free_count = 0;
static size_t strdup_count = 0;

// Custom memory allocation functions with tracking
void* custom_malloc(size_t size)
{
    malloc_count++;
    printf("[CUSTOM MALLOC] Allocating %zu bytes (call #%zu)\n", size, malloc_count);
    return malloc(size);
}

void custom_free(void* ptr)
{
    if (ptr != NULL) {
        free_count++;
        printf("[CUSTOM FREE] Freeing memory (call #%zu)\n", free_count);
    }
    free(ptr);
}

char* custom_strdup(const char* str)
{
    strdup_count++;
    printf("[CUSTOM STRDUP] Duplicating string: '%.20s%s' (call #%zu)\n", 
           str, strlen(str) > 20 ? "..." : "", strdup_count);
    
    if (str == NULL) {
        return NULL;
    }
    
    size_t len = strlen(str) + 1;
    char* dup = custom_malloc(len);
    if (dup != NULL) {
        memcpy(dup, str, len);
    }
    return dup;
}

// Simple RPC method for testing
cJSON* hello_method(mjrpc_func_ctx_t* context, cJSON* params, cJSON* id)
{
    (void) context; // Unused parameter
    (void) id;      // Unused parameter
    const char* name = "World";
    
    if (params && cJSON_IsObject(params)) {
        cJSON* name_item = cJSON_GetObjectItem(params, "name");
        if (cJSON_IsString(name_item)) {
            name = name_item->valuestring;
        }
    }
    
    // This will trigger memory allocation for the string
    char greeting[256];
    snprintf(greeting, sizeof(greeting), "Hello, %s!", name);
    
    return cJSON_CreateString(greeting);
}

int main(void)
{
    printf("=== mjsonrpc Memory Hooks Example ===\n\n");
    
    // Step 1: Set custom memory hooks
    printf("1. Setting custom memory hooks...\n");
    int result = mjrpc_set_memory_hooks(custom_malloc, custom_free, custom_strdup);
    if (result != MJRPC_RET_OK) {
        printf("Failed to set memory hooks!\n");
        return 1;
    }
    printf("Custom memory hooks set successfully!\n\n");
    
    // Step 2: Create handle (this will use custom memory functions)
    printf("2. Creating mjsonrpc handle...\n");
    mjrpc_handle_t* handle = mjrpc_create_handle(0);
    if (!handle) {
        printf("Failed to create handle!\n");
        return 1;
    }
    printf("Handle created successfully!\n\n");
    
    // Step 3: Add a method (this will use custom strdup)
    printf("3. Adding 'hello' method...\n");
    result = mjrpc_add_method(handle, hello_method, "hello", NULL);
    if (result != MJRPC_RET_OK) {
        printf("Failed to add method!\n");
        mjrpc_destroy_handle(handle);
        return 1;
    }
    printf("Method added successfully!\n\n");
    
    // Step 4: Process a request (this may trigger more memory allocations)
    printf("4. Processing JSON-RPC request...\n");
    const char* request_str = "{\"jsonrpc\":\"2.0\",\"method\":\"hello\",\"params\":{\"name\":\"Alice\"},\"id\":1}";
    int ret_code;
    char* response_str = mjrpc_process_str(handle, request_str, &ret_code);
    
    if (response_str) {
        printf("Response: %s\n", response_str);
        free(response_str);  // This uses standard free since mjrpc_process_str uses standard memory allocation internally
    } else {
        printf("Failed to process request (return code: %d)\n", ret_code);
    }
    printf("\n");
    
    // Step 5: Clean up (this will use custom free functions)
    printf("5. Cleaning up...\n");
    mjrpc_destroy_handle(handle);
    printf("Cleanup completed!\n\n");
    
    // Step 6: Reset to default functions
    printf("6. Resetting to default memory functions...\n");
    result = mjrpc_set_memory_hooks(NULL, NULL, NULL);
    if (result != MJRPC_RET_OK) {
        printf("Failed to reset memory hooks!\n");
        return 1;
    }
    printf("Memory hooks reset to defaults!\n\n");
    
    // Print statistics
    printf("=== Memory Usage Statistics ===\n");
    printf("Custom malloc calls: %zu\n", malloc_count);
    printf("Custom free calls:   %zu\n", free_count);
    printf("Custom strdup calls: %zu\n", strdup_count);
    printf("\n");
    
    printf("Example completed successfully!\n");
    return 0;
}
