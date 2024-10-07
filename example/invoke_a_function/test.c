#include "mjsonrpc.h"

#include <stdio.h>
#include <stdlib.h>

const char str[] = "{\"jsonrpc\": \"2.0\", \"method\": \"subtract\", \"params\": [42, 23], \"id\": 1}";
mjrpc_handle_t handle;

cJSON *subtract_handler(mjrpc_ctx_t *ctx, cJSON *params, cJSON *id)
{
    double num0, num1;
    char *str = cJSON_PrintUnformatted(params);
    printf("params: %s\n", str);
    free(str);
    cJSON *param = cJSON_GetArrayItem(params, 0);
    if (param)
    {
        num0 = cJSON_GetNumberValue(param);
        printf("index[0]: %lf\n", num0);
    }
    param = cJSON_GetArrayItem(params, 1);
    if (param)
    {
        num1 = cJSON_GetNumberValue(param);
        printf("index[1]: %lf\n", num1);
    }
    num0 = num0 - num1;
    return cJSON_CreateNumber(num0);
}

int main()
{
    mjrpc_add_method(&handle, subtract_handler, "subtract", NULL);

    char *ret_str;
    int ret_code = mjrpc_process(&handle, str, &ret_str);
    printf("return code: %d, return str: %s\n", ret_code, ret_str);
    free(ret_str);
    return 0;
}
