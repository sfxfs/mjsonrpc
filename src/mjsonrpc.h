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

#ifndef _MJSONRPC_H_
#define _MJSONRPC_H_

#include "cJSON.h"

#define JSON_RPC_2_0_PARSE_ERROR        -32700
#define JSON_RPC_2_0_INVALID_REQUEST    -32600
#define JSON_RPC_2_0_METHOD_NOT_FOUND   -32601
#define JSON_RPC_2_0_INVALID_PARAMS     -32603
#define JSON_RPC_2_0_INTERNAL_ERROR     -32693
// -32000 to -32099 Reserved for implementation-defined server-errors.

enum mjrpc_error_return
{
    MJRPC_RET_OK,
    MJRPC_RET_ERROR_MEM_ALLOC_FAILED,
    MJRPC_RET_ERROR_NOT_FOUND,
    MJRPC_RET_ERROR_EMPTY_REQUST,
    MJRPC_RET_ERROR_PARSE_FAILED,
};

typedef struct
{
    void *data;
    int error_code;
    char *error_message;
} mjrpc_ctx_t;

typedef cJSON *(*mjrpc_func)(mjrpc_ctx_t *context,
                                 cJSON *params,
                                 cJSON *id);

struct mjrpc_cb
{
    char *name;
    mjrpc_func function;
    void *arg;
};

typedef struct mjrpc_handler
{
    int cb_count;
    struct mjrpc_cb *cb_array;
} mjrpc_handler_t;

cJSON *mjrpc_response_ok(cJSON *result, cJSON *id);

cJSON *mjrpc_response_error(int code, char *message, cJSON *id);

int mjrpc_add_method(mjrpc_handler_t *handler,
                        mjrpc_func function_pointer,
                        char *method_name, void *arg2func);

int mjrpc_del_method(mjrpc_handler_t *handler, char *method_name);

char *mjrpc_process_str(mjrpc_handler_t *handler,
                            const char *reqeust_str,
                            int *ret_code);

cJSON *mjrpc_process_cjson(mjrpc_handler_t *handler,
                            cJSON *request_cjson,
                            int *ret_code);

#endif
