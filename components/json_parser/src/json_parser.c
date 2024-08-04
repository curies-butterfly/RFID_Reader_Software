/*
 *    Copyright 2020 Piyush Shah <shahpiyushv@gmail.com>
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#define JSMN_PARENT_LINKS
#define JSMN_STRICT
#define JSMN_STATIC
#include <jsmn.h>
#include <json_parser.h>

// #include "cJSON.h"//2024.7.12
#include <ctype.h>//2024.7.12 add 包含字符处理函数，如isspace


static bool token_matches_str(jparse_ctx_t *ctx, json_tok_t *tok, const char *str)
{
    const char *js = ctx->js;
    return ((strncmp(js + tok->start, str, strlen(str)) == 0)
            && (strlen(str) == (size_t) (tok->end - tok->start)));
}

static json_tok_t *json_skip_elem(json_tok_t *token)
{
    json_tok_t *cur = token;
    int cnt = cur->size;
    while (cnt--) {
        cur++;
        cur = json_skip_elem(cur);
    }
    return cur;
}

static int json_tok_to_bool(jparse_ctx_t *jctx, json_tok_t *tok, bool *val)
{
    if (token_matches_str(jctx, tok, "true") || token_matches_str(jctx, tok, "1")) {
        *val = true;
    } else if  (token_matches_str(jctx, tok, "false") || token_matches_str(jctx, tok, "0")) {
        *val = false;
    } else {
        return -OS_FAIL;
    }
    return OS_SUCCESS;
}

static int json_tok_to_int(jparse_ctx_t *jctx, json_tok_t *tok, int *val)
{
    const char *tok_start = &jctx->js[tok->start];
    const char *tok_end = &jctx->js[tok->end];
    char *endptr;
    int i = strtoul(tok_start, &endptr, 10);
    if (endptr == tok_end) {
        *val = i;
        return OS_SUCCESS;
    }
    return -OS_FAIL;
}

static int json_tok_to_int64(jparse_ctx_t *jctx, json_tok_t *tok, int64_t *val)
{
    const char *tok_start = &jctx->js[tok->start];
    const char *tok_end = &jctx->js[tok->end];
    char *endptr;
    int64_t i64 = strtoull(tok_start, &endptr, 10);
    if (endptr == tok_end) {
        *val = i64;
        return OS_SUCCESS;
    }
    return -OS_FAIL;
}

static int json_tok_to_float(jparse_ctx_t *jctx, json_tok_t *tok, float *val)
{
    const char *tok_start = &jctx->js[tok->start];
    const char *tok_end = &jctx->js[tok->end];
    char *endptr;
    float f = strtof(tok_start, &endptr);
    if (endptr == tok_end) {
        *val = f;
        return OS_SUCCESS;
    }
    return -OS_FAIL;
}

static int json_tok_to_string(jparse_ctx_t *jctx, json_tok_t *tok, char *val, int size)
{
    if ((tok->end - tok->start) > (size - 1)) {
        return -OS_FAIL;
    }
    strncpy(val, jctx->js + tok->start, tok->end - tok->start);
    val[tok->end - tok->start] = 0;
    return OS_SUCCESS;
}

static json_tok_t *json_obj_search(jparse_ctx_t *jctx, const char *key)
{
    json_tok_t *tok = jctx->cur;
    int size = tok->size;
    if (size <= 0) {
        return NULL;
    }
    if (tok->type != JSMN_OBJECT) {
        return NULL;
    }

    while (size--) {
        tok++;
        if (token_matches_str(jctx, tok, key)) {
            return tok;
        }
        tok = json_skip_elem(tok);
    }
    return NULL;
}

static json_tok_t *json_obj_get_val_tok(jparse_ctx_t *jctx, const char *name, jsmntype_t type)
{
    json_tok_t *tok = json_obj_search(jctx, name);
    if (!tok) {
        return NULL;
    }
    tok++;
    if (tok->type != type) {
        return NULL;
    }
    return tok;
}

int json_obj_get_array(jparse_ctx_t *jctx, const char *name, int *num_elem)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_ARRAY);
    if (!tok) {
        return -OS_FAIL;
    }
    jctx->cur = tok;
    *num_elem = tok->size;
    return OS_SUCCESS;
}

int json_obj_leave_array(jparse_ctx_t *jctx)
{
    /* The array's parent will be the key */
    if (jctx->cur->parent < 0) {
        return -OS_FAIL;
    }
    jctx->cur = &jctx->tokens[jctx->cur->parent];

    /* The key's parent will be the actual parent object */
    if (jctx->cur->parent < 0) {
        return -OS_FAIL;
    }
    jctx->cur = &jctx->tokens[jctx->cur->parent];
    return OS_SUCCESS;
}

int json_obj_get_object(jparse_ctx_t *jctx, const char *name)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_OBJECT);
    if (!tok) {
        return -OS_FAIL;
    }
    jctx->cur = tok;
    return OS_SUCCESS;
}

int json_obj_leave_object(jparse_ctx_t *jctx)
{
    /* The objects's parent will be the key */
    if (jctx->cur->parent < 0) {
        return -OS_FAIL;
    }
    jctx->cur = &jctx->tokens[jctx->cur->parent];

    /* The key's parent will be the actual parent object */
    if (jctx->cur->parent < 0) {
        return -OS_FAIL;
    }
    jctx->cur = &jctx->tokens[jctx->cur->parent];
    return OS_SUCCESS;
}

int json_obj_get_bool(jparse_ctx_t *jctx, const char *name, bool *val)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_bool(jctx, tok, val);
}

int json_obj_get_int(jparse_ctx_t *jctx, const char *name, int *val)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_int(jctx, tok, val);
}

int json_obj_get_int64(jparse_ctx_t *jctx, const char *name, int64_t *val)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_int64(jctx, tok, val);
}

int json_obj_get_float(jparse_ctx_t *jctx, const char *name, float *val)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_float(jctx, tok, val);
}

int json_obj_get_string(jparse_ctx_t *jctx, const char *name, char *val, int size)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_STRING);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_string(jctx, tok, val, size);
}

int json_obj_get_strlen(jparse_ctx_t *jctx, const char *name, int *strlen)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_STRING);
    if (!tok) {
        return -OS_FAIL;
    }
    *strlen = tok->end - tok->start;
    return OS_SUCCESS;
}

int json_obj_get_object_str(jparse_ctx_t *jctx, const char *name, char *val, int size)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_OBJECT);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_string(jctx, tok, val, size);
}

int json_obj_get_object_strlen(jparse_ctx_t *jctx, const char *name, int *strlen)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_OBJECT);
    if (!tok) {
        return -OS_FAIL;
    }
    *strlen = tok->end - tok->start;
    return OS_SUCCESS;
}
int json_obj_get_array_str(jparse_ctx_t *jctx, const char *name, char *val, int size)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_ARRAY);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_string(jctx, tok, val, size);
}

int json_obj_get_array_strlen(jparse_ctx_t *jctx, const char *name, int *strlen)
{
    json_tok_t *tok = json_obj_get_val_tok(jctx, name, JSMN_ARRAY);
    if (!tok) {
        return -OS_FAIL;
    }
    *strlen = tok->end - tok->start;
    return OS_SUCCESS;
}

static json_tok_t *json_arr_search(jparse_ctx_t *ctx, uint32_t index)
{
    json_tok_t *tok = ctx->cur;
    if ((tok->type != JSMN_ARRAY) || (tok->size <= 0)) {
        return NULL;
    }
    if (index > (uint32_t)(tok->size - 1)) {
        return NULL;
    }
    /* Increment by 1, so that token points to index 0 */
    tok++;
    while (index--) {
        tok = json_skip_elem(tok);
        tok++;
    }
    return tok;
}
static json_tok_t *json_arr_get_val_tok(jparse_ctx_t *jctx, uint32_t index, jsmntype_t type)
{
    json_tok_t *tok = json_arr_search(jctx, index);
    if (!tok) {
        return NULL;
    }
    if (tok->type != type) {
        return NULL;
    }
    return tok;
}

int json_arr_get_array(jparse_ctx_t *jctx, uint32_t index)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_ARRAY);
    if (!tok) {
        return -OS_FAIL;
    }
    jctx->cur = tok;
    return OS_SUCCESS;
}

int json_arr_leave_array(jparse_ctx_t *jctx)
{
    if (jctx->cur->parent < 0) {
        return -OS_FAIL;
    }
    jctx->cur = &jctx->tokens[jctx->cur->parent];
    return OS_SUCCESS;
}

int json_arr_get_object(jparse_ctx_t *jctx, uint32_t index)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_OBJECT);
    if (!tok) {
        return -OS_FAIL;
    }
    jctx->cur = tok;
    return OS_SUCCESS;
}

int json_arr_leave_object(jparse_ctx_t *jctx)
{
    if (jctx->cur->parent < 0) {
        return -OS_FAIL;
    }
    jctx->cur = &jctx->tokens[jctx->cur->parent];
    return OS_SUCCESS;
}

int json_arr_get_bool(jparse_ctx_t *jctx, uint32_t index, bool *val)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_bool(jctx, tok, val);
}

int json_arr_get_int(jparse_ctx_t *jctx, uint32_t index, int *val)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_int(jctx, tok, val);
}

int json_arr_get_int64(jparse_ctx_t *jctx, uint32_t index, int64_t *val)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_int64(jctx, tok, val);
}

int json_arr_get_float(jparse_ctx_t *jctx, uint32_t index, float *val)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_PRIMITIVE);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_float(jctx, tok, val);
}

int json_arr_get_string(jparse_ctx_t *jctx, uint32_t index, char *val, int size)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_STRING);
    if (!tok) {
        return -OS_FAIL;
    }
    return json_tok_to_string(jctx, tok, val, size);
}

int json_arr_get_strlen(jparse_ctx_t *jctx, uint32_t index, int *strlen)
{
    json_tok_t *tok = json_arr_get_val_tok(jctx, index, JSMN_STRING);
    if (!tok) {
        return -OS_FAIL;
    }
    *strlen = tok->end - tok->start;
    return OS_SUCCESS;
}

int json_parse_start(jparse_ctx_t *jctx, const char *js, int len)
{
    memset(jctx, 0, sizeof(jparse_ctx_t));
    jsmn_init(&jctx->parser);
    int num_tokens = jsmn_parse(&jctx->parser, js, len, NULL, 0);
    if (num_tokens <= 0) {
        return -OS_FAIL;
    }
    jctx->num_tokens = num_tokens;
    jctx->tokens = calloc(num_tokens, sizeof(json_tok_t));
    if (!jctx->tokens) {
        return -OS_FAIL;
    }
    jctx->js = js;
    jsmn_init(&jctx->parser);
    int ret = jsmn_parse(&jctx->parser, js, len, jctx->tokens, jctx->num_tokens);
    if (ret <= 0) {
        free(jctx->tokens);
        memset(jctx, 0, sizeof(jparse_ctx_t));
        return -OS_FAIL;
    }
    jctx->cur = jctx->tokens;
    return OS_SUCCESS;
}

int json_parse_end(jparse_ctx_t *jctx)
{
    if (jctx->tokens) {
        free(jctx->tokens);
    }
    memset(jctx, 0, sizeof(jparse_ctx_t));
    return OS_SUCCESS;
}

int json_parse_start_static(jparse_ctx_t *jctx, const char *js, int len, json_tok_t *buffer_tokens, int buffer_tokens_max_count)
{
    // Init
    memset(buffer_tokens, 0, buffer_tokens_max_count * sizeof(json_tok_t));
    memset(jctx, 0, sizeof(jparse_ctx_t));

    // Check fit
    jsmn_init(&jctx->parser);
    int num_tokens = jsmn_parse(&jctx->parser, js, len, NULL, 0);
    if (num_tokens <= 0 || num_tokens > buffer_tokens_max_count) {
        return -OS_FAIL;
    }

    // Set struct
    jctx->num_tokens = num_tokens;
    jctx->tokens = buffer_tokens;
    jctx->js = js;

    // Parse
    jsmn_init(&jctx->parser);
    int ret = jsmn_parse(&jctx->parser, js, len, jctx->tokens, jctx->num_tokens);
    if (ret <= 0) {
        memset(jctx, 0, sizeof(jparse_ctx_t));
        return -OS_FAIL;
    }
    jctx->cur = jctx->tokens;
    return OS_SUCCESS;
}

int json_parse_end_static(jparse_ctx_t *jctx)
{
    memset(jctx, 0, sizeof(jparse_ctx_t));
    return OS_SUCCESS;
}



//2024.7.12

// //判断是否是json格式数据
// bool is_valid_json(const char *data, int data_len) {
//     // 为数据副本分配内存并确保其以 '\0' 结尾
//     char *data_copy = strndup(data, data_len);
//     if (data_copy == NULL) {
//         ESP_LOGE(TAG, "Failed to allocate memory for data copy");
//         return false;
//     }

//     // 尝试解析 JSON 数据
//     cJSON *json = cJSON_Parse(data_copy);
//     free(data_copy); // 立即释放数据副本

//     // 检查解析结果
//     if (json == NULL) {
//         return false;
//     }

//     // 解析成功，释放 JSON 对象并返回 true
//     cJSON_Delete(json);
//     return true;
// }
// 判断是否为 JSON 数据格式
bool is_json_data(const char *data, int data_len) {
    if (data == NULL || data_len <= 0) {
        return false;
    }

    // 跳过空白字符
    int start = 0;
    while (start < data_len && isspace((unsigned char)data[start])) {
        start++;
    }

    // 开头必须是 '{' 或 '['
    if (start >= data_len || (data[start] != '{' && data[start] != '[')) {
        return false;
    }

    // 结尾必须是 '}' 或 ']'
    int end = data_len - 1;
    while (end >= 0 && isspace((unsigned char)data[end])) {
        end--;
    }
    
    if (end < 0 || (data[end] != '}' && data[end] != ']')) {
        return false;
    }

    // 可以添加更多逻辑来验证引号配对等

    return true;
}




