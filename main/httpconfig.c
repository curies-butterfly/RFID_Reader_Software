#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_tls_crypto.h"
#include "esp_vfs.h"
#include "json_parser.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "httpconfig.h"
#include "parameter.h"
#include <time.h>
#include <sys/time.h>
#include "sht30.h"
#include "rfidmodule.h"

#define debug_flag 1
#define debug_ "*"
#define online "http://localhost:8080"

static const char *TAG = "rfid_reader_http_server";
#define HTTPD_401 "401 UNAUTHORIZED"                /*!< HTTP Response 401 */
static bool s_if_init = false;                      //http server 是否已经初始化
static httpd_handle_t s_server = NULL;

#define REST_CHECK(a, str, goto_tag, ...)                                         \
    do {                                                                          \
        if (!(a)) {                                                               \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/**
 * @brief Store the currently connected sta
 *
 */
#define STA_CHECK(a, str, ret)                                                 \
    if (!(a)) {                                                                \
        ESP_LOGE(TAG, "%s:%d (%s):%s", __FILE__, __LINE__, __FUNCTION__, str); \
        return (ret);                                                          \
    }
#define STA_CHECK_GOTO(a, str, label)                                          \
    if (!(a)) {                                                                \
        ESP_LOGE(TAG, "%s:%d (%s):%s", __FILE__, __LINE__, __FUNCTION__, str); \
        goto label;                                                            \
    }

#define STA_NODE_MUTEX_TICKS_TO_WAIT 200
static SemaphoreHandle_t s_sta_node_mutex = NULL;
static modem_http_list_head_t s_sta_list_head = SLIST_HEAD_INITIALIZER(s_sta_list_head);


static void restart()
{
    ESP_LOGI(TAG, "Restarting now.\n");
    fflush(stdout);
    esp_restart();
}

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

typedef struct {
    char *username;
    char *password;
} basic_auth_info_t;

typedef struct {
    rest_server_context_t *rest_context;
    basic_auth_info_t *basic_auth_info;
} ctx_info_t;

static char *http_auth_basic(const char *username, const char *password)
{
    int out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    asprintf(&user_info, "%s:%s", username, password);
    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
     */
    digest = calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info,
                                 strlen(user_info));
    }
    free(user_info);
    return digest;
}

static esp_err_t basic_auth_get(httpd_req_t *req)
{
    char *buf = NULL;
    size_t buf_len = 0;
    ctx_info_t *ctx_info = req->user_ctx;
    basic_auth_info_t *basic_auth_info = ctx_info->basic_auth_info;
    get_nvs_auth_info_config();
    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            return ESP_ERR_NO_MEM;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            ESP_LOGD(TAG, "Found header => Authorization: %s", buf);
        } else {
            ESP_LOGE(TAG, "No auth value received");
        }

        char *auth_credentials = http_auth_basic(basic_auth_info->username, basic_auth_info->password);
        if (!auth_credentials) {
            ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
            free(buf);
            return ESP_ERR_NO_MEM;
        } else {
            ESP_LOGD(TAG, "auth_credentials : %s", auth_credentials);
        }

        if (strncmp(auth_credentials, buf, buf_len)) {
            ESP_LOGE(TAG, "Not Authenticated");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"router\"");
            httpd_resp_send(req, NULL, 0);
            free(auth_credentials);
            free(buf);
            return ESP_FAIL;
        } else {
            ESP_LOGD(TAG, "Authenticated!");
            httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            free(auth_credentials);
            free(buf);
            return ESP_OK;
        }
    } else {
        ESP_LOGE(TAG, "No auth header received");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"router\"");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void delete_char(char *str, char target)
{
    int i, j;
    for (i = j = 0; str[i] != '\0'; i++) {
        if (str[i] != target) {
            str[j++] = str[i];
        }
    }
    str[j] = '\0';
}



static esp_err_t init_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_EXAMPLE_WEB_MOUNT_POINT,
        .partition_label = NULL,
        /*Maximum files that could be open at the same time.*/
        .max_files = 1,
        .format_if_mount_failed = true
    };
    /*Register and mount SPIFFS to VFS with given path prefix.*/
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}



esp_err_t modem_http_print_nodes(modem_http_list_head_t *head)
{
    struct modem_netif_sta_info *node;
    SLIST_FOREACH(node, head, field) {
        ESP_LOGI(TAG, "MAC is " MACSTR ", IP is " IPSTR ", start_time is %lld ", MAC2STR(node->mac),
                 IP2STR(&node->ip), node->start_time);
    }
    return ESP_OK;
}

static esp_err_t stalist_update()
{
    if (pdTRUE == xSemaphoreTake(s_sta_node_mutex, STA_NODE_MUTEX_TICKS_TO_WAIT)) {
        struct modem_netif_sta_info *node;
        SLIST_FOREACH(node, &s_sta_list_head, field) {
            if (node->ip.addr == 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
                esp_netif_pair_mac_ip_t pair_mac_ip = { 0 };
                memcpy(pair_mac_ip.mac, node->mac, 6);
                esp_netif_dhcps_get_clients_by_mac(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), 1, &pair_mac_ip);
                node->ip = pair_mac_ip.ip;
#else
                dhcp_search_ip_on_mac(node->mac, (ip4_addr_t *)&node->ip);
#endif
            }
            char mac_addr[18] = "";
            size_t name_size = sizeof(node->name);
            sprintf(mac_addr, "%02x%02x%02x%02x%02x%02x", node->mac[0], node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5]);
            from_nvs_get_value(mac_addr, node->name, &name_size);
        }
        if (!(pdTRUE == xSemaphoreGive(s_sta_node_mutex))) {
            ESP_LOGE(TAG, "give semaphore failed");
        };
    }
    modem_http_print_nodes(&s_sta_list_head);
    return ESP_OK;
}

modem_http_list_head_t *modem_http_get_stalist(){
    return &s_sta_list_head;
}

static esp_err_t stalist_add_node(uint8_t mac[6])
{
    // STA_CHECK(sta != NULL, "sta pointer can not be NULL", ESP_ERR_INVALID_ARG);
    struct modem_netif_sta_info *node = calloc(1, sizeof(struct modem_netif_sta_info));
    STA_CHECK(node != NULL, "calloc node failed", ESP_ERR_NO_MEM);
    STA_CHECK_GOTO(pdTRUE == xSemaphoreTake(s_sta_node_mutex, STA_NODE_MUTEX_TICKS_TO_WAIT), "take semaphore timeout", cleanupnode);
    node->start_time = esp_timer_get_time();
    memcpy(node->mac, mac, 6);
    char mac_addr[18] = "";
    size_t name_size = sizeof(node->name);
    sprintf(mac_addr, "%02x%02x%02x%02x%02x%02x", node->mac[0], node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5]);
    esp_err_t err = from_nvs_get_value(mac_addr, node->name, &name_size); // name
    if (err == ESP_ERR_NVS_NOT_FOUND ) {
        memcpy(node->name, mac_addr, 12);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_netif_pair_mac_ip_t pair_mac_ip = { 0 };
    memcpy(pair_mac_ip.mac, node->mac, 6);
    esp_netif_dhcps_get_clients_by_mac(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), 1, &pair_mac_ip);
    node->ip = pair_mac_ip.ip;
#else
    dhcp_search_ip_on_mac(node->mac, (ip4_addr_t *)&node->ip);
#endif
    SLIST_INSERT_HEAD(&s_sta_list_head, node, field);
    STA_CHECK_GOTO(pdTRUE == xSemaphoreGive(s_sta_node_mutex), "give semaphore failed", cleanupnode);
    return ESP_OK;
cleanupnode:
    free(node);
    return ESP_FAIL;
}

static esp_err_t sta_remove_node(uint8_t mac[6])
{
    struct modem_netif_sta_info *node;
    STA_CHECK(pdTRUE == xSemaphoreTake(s_sta_node_mutex, STA_NODE_MUTEX_TICKS_TO_WAIT), "take semaphore timeout", ESP_ERR_TIMEOUT);
    SLIST_FOREACH(node, &s_sta_list_head, field) {
        if (!memcmp(node->mac, mac, 6)) {
            ESP_LOGI(TAG, "remove MAC is " MACSTR ", IP is " IPSTR ", start_time is %lld ", MAC2STR(node->mac),
                     IP2STR(&node->ip), node->start_time);
            SLIST_REMOVE(&s_sta_list_head, node, modem_netif_sta_info, field);
            free(node);
            break;
        }
    }
    STA_CHECK(pdTRUE == xSemaphoreGive(s_sta_node_mutex), "give semaphore failed", ESP_FAIL);
    return ESP_OK;
}


static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}


static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    // esp_err_t err = basic_auth_get(req);
    // REST_CHECK(err == ESP_OK, "not login yet", end);

    ESP_LOGD(TAG, "(%s) %s", __func__, req->uri);
    char filepath[FILE_PATH_MAX];

    //rest_server_context_t* rest_context = (rest_server_context_t*)req->user_ctx->rest_context;
    ctx_info_t *ctx_info = (ctx_info_t *)req->user_ctx;
    rest_server_context_t *rest_context = ctx_info->rest_context;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }  
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGD(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
// end:
//     return ESP_OK;
}


static esp_err_t login_post_handler(httpd_req_t *req)
{
    char user_info_name[32] = {'\0'};
    char user_info_password[32] = {'\0'};
    char buf[256] = { 0 };
    int len_ret, remaining = req->content_len;
    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }
    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;
        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }

    
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif

    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));
    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];
    if (json_obj_get_string(&jctx, "name", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_info_name, sizeof(user_info_name), "%.*s", sizeof(user_info_name) - 1, str_val);
        // ESP_LOGI(TAG, "user_info_name: %s\n", user_info_name);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "password", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_info_password, sizeof(user_info_password), "%.*s", sizeof(user_info_password) - 1, str_val);
        // ESP_LOGI(TAG, "user_info_password: %s\n", user_info_password);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    char *user_info_req = NULL;
    char *user_info_sys = NULL;
    asprintf(&user_info_req, "%s:%s", user_info_name, user_info_password);
    if (!user_info_req) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    asprintf(&user_info_sys, "%s:%s", web_auth_info.username, web_auth_info.password);
    if (!user_info_sys) {
        free(user_info_req);
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    char *json_str = NULL;
    size_t size = 0;

    if (strncmp(user_info_req, user_info_sys,  strlen(user_info_sys))) {
        size = asprintf(&json_str,"{\"status\":\"%s\"}","error");  
        esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
        ESP_ERROR_CHECK(ret);
        ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
        ESP_ERROR_CHECK(ret);
        ret = httpd_resp_send(req, json_str, size);
        free(json_str);
        free(user_info_req);
        free(user_info_sys);
        ESP_ERROR_CHECK(ret);
        return ESP_FAIL;
    } else {
        size = asprintf(&json_str,"{\"status\":\"%s\",\"name\":\"%s\",\"password\":\"%s\"}","ok", user_info_name, user_info_password);  
        esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
        ESP_ERROR_CHECK(ret);
        ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
        ESP_ERROR_CHECK(ret);
        ret = httpd_resp_send(req, json_str, size);
        free(json_str);
        free(user_info_req);
        free(user_info_sys);
        ESP_ERROR_CHECK(ret);
        return ESP_OK;
    }
    return ESP_OK;
}

static httpd_uri_t login_post = {
    .uri = "/login",
    .method = HTTP_POST,
    .handler = login_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};


static esp_err_t auth_info_get_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    const char *user_name = web_auth_info.username;
    const char *user_password = web_auth_info.password;
    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,
                    "{\"status\":\"200\", \"name\":\"%s\", \"password\":\"%s\"}",
                    user_name, user_password);
    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
 
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif

    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

     /**
     * @brief Set some custom headers
     */

    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
end:
    return ESP_OK;
}


static httpd_uri_t auth_info_get = {
    .uri = "/auth_info",
    .method = HTTP_GET,
    .handler = auth_info_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static esp_err_t auth_info_post_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    char user_name[20] = "";
    char user_password[20] = "";
    char buf[256] = { 0 };
    int len_ret, remaining = req->content_len;

    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }
    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));

    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];
    if (json_obj_get_string(&jctx, "name", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_name, sizeof(user_name), "%.*s", sizeof(user_name) - 1, str_val);
        ESP_LOGI(TAG, "name %s\n", user_name);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }
    if (json_obj_get_string(&jctx, "password", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_password, sizeof(user_password), "%.*s", sizeof(user_password) - 1, str_val);
        ESP_LOGI(TAG, "password %s\n", user_password);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }
    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,
                    "{\"status\":\"200\", \"name\":\"%s\", \"password\":\"%s\"}",
                    user_name, user_password);
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);

    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(from_nvs_set_value("auth_username", user_name));
    ESP_ERROR_CHECK(from_nvs_set_value("auth_password", user_password));
    return ESP_OK;
end:
    return ESP_OK;
}


static httpd_uri_t auth_info_post = {
    .uri = "/auth_info",
    .method = HTTP_POST,
    .handler = auth_info_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static esp_err_t wlan_general_get_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    get_nvs_wifi_config(s_modem_wifi_config);
    const char *user_ssid = s_modem_wifi_config->ssid;
    const char *user_password = s_modem_wifi_config->password;
    const char *user_hide_ssid ;
    if (s_modem_wifi_config->ssid_hidden == 0) {
        user_hide_ssid = "false";
    } else {
        user_hide_ssid = "true";
    }

    const char *user_auth_mode;
    switch (s_modem_wifi_config->authmode) {
    case WIFI_AUTH_OPEN:
        user_auth_mode = "OPEN";
        break;

    case WIFI_AUTH_WEP:
        user_auth_mode = "WEP";
        break;

    case WIFI_AUTH_WPA2_PSK:
        user_auth_mode = "WAP2_PSK";
        break;

    case WIFI_AUTH_WPA_WPA2_PSK:
        user_auth_mode = "WPA_WPA2_PSK";
        break;

    default:
        user_auth_mode = "WPA_WPA2_PSK";
        break;
    }

    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,
                    "{\"status\":\"200\", \"ssid\":\"%s\", \"if_hide_ssid\":\"%s\", "
                    "\"auth_mode\":\"%s\", \"password\":\"%s\"}",
                    user_ssid, user_hide_ssid, user_auth_mode, user_password);

    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

    /**
     * @brief Set some custom headers
     */

    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
end:
    return ESP_OK;
}

static httpd_uri_t wlan_general = {
    .uri = "/wlan_general",
    .method = HTTP_GET,
    .handler = wlan_general_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};


static esp_err_t wlan_general_post_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    char user_ssid[32] = "";
    char user_password[64] = "";
    char user_hide_ssid[8] = "";
    char user_auth_mode[16] = "";

    char buf[256] = { 0 };
    int len_ret, remaining = req->content_len;

    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));

    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];

    if (json_obj_get_string(&jctx, "ssid", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_ssid, sizeof(user_ssid), "%.*s", sizeof(user_ssid) - 1, str_val);
        ESP_LOGI(TAG, "ssid %s\n", user_ssid);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "if_hide_ssid", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_hide_ssid, sizeof(user_hide_ssid), "%.*s", sizeof(user_hide_ssid) - 1, str_val);
        ESP_LOGI(TAG, "if_hide_ssid %s\n", user_hide_ssid);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "auth_mode", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_auth_mode, sizeof(user_auth_mode), "%.*s", sizeof(user_auth_mode) - 1, str_val);
        ESP_LOGI(TAG, "auth_mode %s\n", user_auth_mode);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "password", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_password, sizeof(user_password), "%.*s", sizeof(user_password) - 1, str_val);
        ESP_LOGI(TAG, "password %s\n", user_password);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(from_nvs_set_value("ssid", user_ssid));
    ESP_ERROR_CHECK(from_nvs_set_value("password", user_password));

    if ( !strcmp(user_hide_ssid, "true") ) {
        ESP_ERROR_CHECK(from_nvs_set_value("hide_ssid", "true"));
    } else if (
        !strcmp(user_hide_ssid, "false") ) {
        ESP_ERROR_CHECK(from_nvs_set_value("hide_ssid", "false"));
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter user_hide_ssid error");
        return ESP_FAIL;
    }

     if ( !strcmp(user_auth_mode, "OPEN") ) {
        ESP_ERROR_CHECK(from_nvs_set_value("auth_mode", "OPEN"));
    } else if ( !strcmp(user_auth_mode, "WEP") ) {
        ESP_ERROR_CHECK(from_nvs_set_value("auth_mode", "WEP"));
    } else if ( !strcmp(user_auth_mode, "WPA2_PSK") ) {
        ESP_ERROR_CHECK(from_nvs_set_value("auth_mode", "WPA2_PSK"));
    } else if ( !strcmp(user_auth_mode, "WPA_WPA2_PSK") ) {
        ESP_ERROR_CHECK(from_nvs_set_value("auth_mode", "WPA_WPA2_PSK"));
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter user_auth_mode error");
        return ESP_FAIL;
    }

    char *json_str = NULL;
    size_t size = 0;
    // size = asprintf(&json_str,
    //                 "{\"status\":\"200\", \"ssid\":\"%s\", \"if_hide_ssid\":\"%s\", "
    //                 "\"auth_mode\":\"%s\", \"password\":\"%s\"}",
    //                 user_ssid, user_hide_ssid, user_auth_mode, user_password);
    size = asprintf(&json_str,"{\"status\":\"200\"}");
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);

    //restart();
    return ESP_OK;
end:
    return ESP_OK;
}

static httpd_uri_t wlan_general_post = {
    .uri = "/wlan_general",
    .method = HTTP_POST,
    .handler = wlan_general_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static esp_err_t sys_sta_get_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    time_t now_time; 
    time(&now_time);
    uint8_t err_code = 0x11;
    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,
                "{\"status\":\"200\", \"time\":\"%lld\",\"err_code\":\"%x\", \"tempt\":\"%.2f\",\"humt\":\"%.2f\"}",
                    now_time, err_code, sht30_data.Temperature, sht30_data.Humidity);
    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

     /**
     * @brief Set some custom headers
     */
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
end:
    return ESP_OK;
}

static httpd_uri_t sys_sta_get = {
    .uri = "/sys_sta",
    .method = HTTP_GET,
    .handler = sys_sta_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};


static esp_err_t sys_restart_get_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,"{\"status\":\"200\"}");
    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

     /**
     * @brief Set some custom headers
     */
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    vTaskDelay(1000/portTICK_PERIOD_MS);
    restart();
    return ESP_OK;
end:
    return ESP_OK;
}

static httpd_uri_t sys_restart_get = {
    .uri = "/sys_restart",
    .method = HTTP_GET,
    .handler = sys_restart_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};


static esp_err_t rfid_epc_get_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    EPC_Info_t  **EPC_ptr = &LTU3_Lable;
    char  *epc_data = NULL;
    if(epcCnt == 0)         //防止出现空指针
    {
        epc_data = (char*)malloc(sizeof(char) * 1 * 120);
        memset(epc_data,'\0',sizeof(char) * 1 * 120);
    }
    else
    {
        epc_data = (char*)malloc(sizeof(char) * epcCnt * 120);
        memset(epc_data,'\0',sizeof(char) * epcCnt * 120);
    }
    char  one_epc_data[120] = {'\0'};
    uint16_t epc_read_rate = epc_read_speed;
    epc_read_speed = 0;
    for(int i = 0; i < 120; i++)
    {
        if(EPC_ptr[i] == NULL)
            break;
        memset(one_epc_data,0,sizeof(one_epc_data));
        sprintf(one_epc_data,"{\"epc\":\"%02x%02x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",              \
        EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->tempe/100.0,EPC_ptr[i]->antID,EPC_ptr[i]->rssi); 
        strcat(epc_data,one_epc_data);
        strcat(epc_data,",");
    }
    epc_data[strlen(epc_data) - 1] = '\0';
    size_t size = 0;
    char *json_str = NULL;
    size = asprintf(&json_str,"{\"status\":\"200\",\"epc_cnt\":\"%d\",\"read_rate\":\"%d\",\"data\":[%s]}",\
            epcCnt,epc_read_rate,epc_data);
    free(epc_data);
    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

     /**
     * @brief Set some custom headers
     */
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
end:
    return ESP_OK;
}

static httpd_uri_t rfid_epc_get = {
    .uri = "/rfid_epc",
    .method = HTTP_GET,
    .handler = rfid_epc_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};


static esp_err_t sys_info_get_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    get_nvs_sys_info_config();
    char *hard_ver = sys_info_config.sys_hard_version;
    char *soft_ver = sys_info_config.sys_soft_version;
    char *work_mode = NULL;
    switch (sys_info_config.sys_work_mode)
    {
    case SYS_WORK_MODE_READER:
        work_mode = "rfid reader";
        break;
    case SYS_WORK_MODE_GATEWAY:
        work_mode = "gateway";
        break;
    default:
        work_mode = "unknown";
        break;
    }
    char *net_sel = NULL;
    switch (sys_info_config.sys_networking_mode)
    {
    case SYS_NETWORKING_4G:
        net_sel = "4G";
        break;
    case SYS_NETWORKING_ETHERNET:
        net_sel = "ethernet";
        break;
    case SYS_NETWORKING_ALL:
        net_sel = "4G+ethernet";
        break;
    default:
        net_sel = "unknown";
        break;
    }

    char *net_protocol = NULL;
    switch (sys_info_config.sys_net_communication_protocol)
    {
    case SYS_NET_COMMUNICATION_PROTOCOL_MQTT:
        net_protocol = "mqtt";
        break;
    case SYS_NET_COMMUNICATION_PROTOCOL_TCP:
        net_protocol = "tcp";
        break;
    default:
        net_protocol = "unknown";
        break;
    }
    char *mqtt_addr = sys_info_config.mqtt_address;
    char *tcp_addr = sys_info_config.tcp_address;
    int tcp_port = sys_info_config.tcp_port;
    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,"{\"status\":\"200\",\"hard_ver\":\"%s\",\"soft_ver\":\"%s\",\"work_mode\":\"%s\",\"net_sel\":\"%s\", \
                    \"net_protocol\":\"%s\", \"mqtt_addr\":\"%s\", \"tcp_addr\":\"%s\", \"tcp_port\":\"%d\"}",
                    hard_ver, soft_ver, work_mode, net_sel, net_protocol, mqtt_addr, tcp_addr, tcp_port);

    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

     /**
     * @brief Set some custom headers
     */
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
end:
    return ESP_OK;
}

static httpd_uri_t sys_info_get = {
    .uri = "/sys_info",
    .method = HTTP_GET,
    .handler = sys_info_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};


static esp_err_t sys_info_post_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    char work_mode[32] = "";
    char net_sel[32] = "";
    char net_protocol[32] = "";
    char mqtt_addr[60] = "";
    char tcp_addr[32] = "";
    char tcp_port[10] = "";

    char buf[512] = { 0 };
    int len_ret, remaining = req->content_len;
    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }

     while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));
    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];

    if (json_obj_get_string(&jctx, "work_mode", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(work_mode, sizeof(work_mode), "%.*s", sizeof(work_mode) - 1, str_val);
        ESP_LOGI(TAG, "system Work mode: %s\n", work_mode);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "net_sel", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(net_sel, sizeof(net_sel), "%.*s", sizeof(net_sel) - 1, str_val);
        ESP_LOGI(TAG, "system networking mode: %s\n", net_sel);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "net_protocol", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(net_protocol, sizeof(net_protocol), "%.*s", sizeof(net_protocol) - 1, str_val);
        ESP_LOGI(TAG, "system communication protocol: %s\n", net_protocol);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "mqtt_addr", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(mqtt_addr, sizeof(mqtt_addr), "%.*s", sizeof(mqtt_addr) - 1, str_val);
        ESP_LOGI(TAG, "mqtt address: %s\n", mqtt_addr);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "tcp_addr", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(tcp_addr, sizeof(tcp_addr), "%.*s", sizeof(tcp_addr) - 1, str_val);
        ESP_LOGI(TAG, "tcp address: %s\n", tcp_addr);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if(json_obj_get_string(&jctx, "tcp_port", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(tcp_port, sizeof(tcp_port), "%.*s", sizeof(tcp_port) - 1, str_val);
        ESP_LOGI(TAG, "tcp port: %s\n", tcp_port);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if(!strcmp(work_mode, "rfid reader"))
        ESP_ERROR_CHECK(from_nvs_set_value("w_mod", "rfid reader"));
    else if(!strcmp(work_mode, "gateway"))
        ESP_ERROR_CHECK(from_nvs_set_value("w_mod", "gateway"));
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter work_mode error");
        return ESP_FAIL;
    }

    if(!strcmp(net_sel, "4G"))
        ESP_ERROR_CHECK(from_nvs_set_value("net_sel", "4G"));
    else if(!strcmp(net_sel, "ethernet"))
        ESP_ERROR_CHECK(from_nvs_set_value("net_sel", "ethernet"));
    else if(!strcmp(net_sel, "4G+ethernet"))
        ESP_ERROR_CHECK(from_nvs_set_value("net_sel", "4G+ethernrt"));
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter net_sel error");
        return ESP_FAIL;
    }

    if(!strcmp(net_protocol, "mqtt"))
        ESP_ERROR_CHECK(from_nvs_set_value("ncp_sel", "mqtt"));
    else if(!strcmp(net_protocol, "tcp"))
        ESP_ERROR_CHECK(from_nvs_set_value("ncp_sel", "tcp"));
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter net_protocol error");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(from_nvs_set_value("mqtt_addr", mqtt_addr));
    ESP_ERROR_CHECK(from_nvs_set_value("tcp_addr", tcp_addr));
    ESP_ERROR_CHECK(from_nvs_set_value("tcp_port", tcp_port));

    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str, "{\"status\":\"200\"}");
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);

end:
    return ESP_OK;

}

static httpd_uri_t sys_info_post = {
    .uri = "/sys_info",
    .method = HTTP_POST,
    .handler = sys_info_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};


static esp_err_t rfid_read_post_handler(httpd_req_t *req)
{
    esp_err_t err = basic_auth_get(req);
    REST_CHECK(err == ESP_OK, "not login yet", end);
    char on_off[5] = "";
    char label_mode[20] = "";
    char read_mode[20] = "";
    char ant_sel[4] = "";
    char interval_time[5] = "";
    char buf[512] = { 0 };
    rfid_read_config_t rfid_read_config;
    int len_ret, remaining = req->content_len;
    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
     
    #ifdef debug_flag
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", debug_);  // 调试模式， 允许所有来源
    #else
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", online);  // 非调试模式，"http://localhost:8080"
    #endif
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));
    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }
    char str_val[64];
    if (json_obj_get_string(&jctx, "on_off", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(on_off, sizeof(on_off), "%.*s", sizeof(on_off) - 1, str_val);
        ESP_LOGI(TAG, "rfid read control: %s\n", on_off);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "label_mode", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(label_mode, sizeof(label_mode), "%.*s", sizeof(label_mode) - 1, str_val);
        ESP_LOGI(TAG, "label_mode: %s\n", label_mode);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "read_mode", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(read_mode, sizeof(read_mode), "%.*s", sizeof(read_mode) - 1, str_val);
        ESP_LOGI(TAG, "rfid read mode: %s\n", read_mode);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "ant_sel", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(ant_sel, sizeof(ant_sel), "%.*s", sizeof(ant_sel) - 1, str_val);
        ESP_LOGI(TAG, "rfid read ant: %s\n", ant_sel);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "interval_time", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(interval_time, sizeof(interval_time), "%.*s", sizeof(interval_time) - 1, str_val);
        ESP_LOGI(TAG, "rfid read interval time: %s\n", interval_time);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if(!strcmp(on_off, "on"))
        rfid_read_config.rfid_read_on_off = RFID_READ_ON;
    else if(!strcmp(on_off, "off"))
        rfid_read_config.rfid_read_on_off = RFID_READ_OFF;
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter on_off error");
        return ESP_FAIL;
    }

    if(!strcmp(label_mode, "yuehe"))
        type_epc= TAG_TYPE_YH;//悦和 TAG_TYPE_YH
    else if(!strcmp(label_mode, "xingyan"))
        // rfid_read_config.rfid_read_on_off = RFID_READ_OFF;
        type_epc= TAG_TYPE_XY;//星沿 TAG_TYPE_XY
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter label_mode error");
        return ESP_FAIL;
    }

    if(!strcmp(read_mode, "once"))
        rfid_read_config.rfid_read_mode = RFID_READ_MODE_ONCE;
    else if(!strcmp(read_mode, "continuous"))
        rfid_read_config.rfid_read_mode = RFID_READ_MODE_CONTINUOUS;
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter read_mode error");
        return ESP_FAIL;
    }
    rfid_read_config.ant_sel = atoi(ant_sel);
    rfid_read_config.read_interval_time = atoi(interval_time);
    printf("ant_sel:%x\r\n", rfid_read_config.ant_sel);
    printf("read_interval_time:%ld\r\n", rfid_read_config.read_interval_time);
    
    char *json_str = NULL;
    size_t size = 0;
    if(RFID_ReadEPC(rfid_read_config) != Ok)
        size = asprintf(&json_str, "{\"status\":\"200\",\"result\":\"failed\"}");
    else
        size = asprintf(&json_str, "{\"status\":\"200\",\"result\":\"success\"}");
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);



    // 构建字符串值
    const char *label_mode_str;
    if (type_epc == TAG_TYPE_YH) {
        label_mode_str = "YH";
    } else if (type_epc == TAG_TYPE_XY) {
        label_mode_str = "XY";
    } else {
        label_mode_str = "UNKNOWN";
    }

    //nvs写入操作
    nvs_handle_t my_handle;//nvs句柄
    esp_err_t ret_nvs=nvs_open("memory",NVS_READWRITE,&my_handle);
    if(ret_nvs!=ESP_OK){
        ESP_LOGE(TAG,"nvs_open error");
    }else{
        //写入数据
        ret_nvs = nvs_set_str(my_handle,"label_mode",label_mode_str);
        if(ret!=ESP_OK){
            ESP_LOGE(TAG,"nvs_set_str error");
        }else{
            ret=nvs_commit(my_handle);//提交数据
            ESP_LOGI(TAG, "nvs_set_str success, label_mode = %s", label_mode_str);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "NVS close Done\n");
        }
    }

    // nvs_close(my_handle);//关闭nvs句柄

    // 更新nvs中的label_mode 键值
    // nvs写入 label_mode 字符串
    // nvs_handle_t my_handle; // nvs句柄
    // esp_err_t ret_nvs = nvs_open("storage", NVS_READWRITE, &my_handle);
    // if (ret_nvs != ESP_OK) {
    //     ESP_LOGE(TAG, "nvs_open error");
    // } else {
       

    //     // 写入数据
    //     ret_nvs = nvs_set_str(my_handle, "label_mode", label_mode_str);
    //     if (ret_nvs != ESP_OK) {
    //         ESP_LOGE(TAG, "nvs_set_str error");
    //     } else {
    //         ret_nvs = nvs_commit(my_handle); // 提交数据
    //         if (ret == ESP_OK) {
    //             ESP_LOGI(TAG, "nvs_set_str success, label_mode = %s", label_mode_str);
    //         } else {
    //             ESP_LOGE(TAG, "nvs_commit error");
    //         }
    //     }
    //     nvs_close(my_handle); // 关闭nvs句柄
    // }


end:
    return ESP_OK;

}

static httpd_uri_t rfid_read_post = {
    .uri = "/rfid_read",
    .method = HTTP_POST,
    .handler = rfid_read_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_handle_t start_webserver(const char *base_path)
{
    ctx_info_t *ctx_info = calloc(1, sizeof(ctx_info_t));
    REST_CHECK(base_path, "wrong base path", err);
    ctx_info->rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(ctx_info->rest_context, "No memory for rest context", err);
    strlcpy(ctx_info->rest_context->base_path, base_path, sizeof(ctx_info->rest_context->base_path));

    ctx_info->basic_auth_info = calloc(1, sizeof(basic_auth_info_t));
    ctx_info->basic_auth_info->username = web_auth_info.username;
    ctx_info->basic_auth_info->password = web_auth_info.password;

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK) {

        // Set URI handlers & Add user_ctx
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &login_post);
        wlan_general.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wlan_general);
        wlan_general_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wlan_general_post);
        auth_info_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &auth_info_get);
        auth_info_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &auth_info_post);
        sys_sta_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &sys_sta_get);
        sys_restart_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &sys_restart_get);
        rfid_epc_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &rfid_epc_get);
        sys_info_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &sys_info_get);   
        sys_info_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &sys_info_post);
        rfid_read_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &rfid_read_post);
        // wlan_advance.user_ctx = ctx_info;
        // httpd_register_uri_handler(server, &wlan_advance);
        // wlan_advance_post.user_ctx = ctx_info;
        // httpd_register_uri_handler(server, &wlan_advance_post);
        // system_station_get.user_ctx = ctx_info;
        // httpd_register_uri_handler(server, &system_station_get);
        // system_station_delete_device_post.user_ctx = ctx_info;
        // httpd_register_uri_handler(server, &system_station_delete_device_post);
        // system_station_change_name_post.user_ctx = ctx_info;
        // httpd_register_uri_handler(server, &system_station_change_name_post);
        // login_get.user_ctx = ctx_info;
        // httpd_register_uri_handler(server, &login_get);
        // login_post.user_ctx = ctx_info;
        // httpd_register_uri_handler(server, &login_post);
        httpd_uri_t common_get_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = rest_common_get_handler,
            .user_ctx = ctx_info
        };
        httpd_register_uri_handler(server, &common_get_uri);

        return server;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return NULL;
err:
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}


static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        sta_remove_node(event->mac);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        stalist_add_node(event->mac);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        stalist_update();
    }
}

esp_err_t rfid_http_init(modem_wifi_config_t *wifi_config)
{
    if(s_if_init == false){
        s_modem_wifi_config = wifi_config;
        SLIST_INIT(&s_sta_list_head);
        /* Start the server for the first time */
        ESP_ERROR_CHECK(init_fs());
        
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
        s_server = start_webserver(CONFIG_EXAMPLE_WEB_MOUNT_POINT);
        ESP_LOGI(TAG, "Starting webserver");
        s_sta_node_mutex = xSemaphoreCreateMutex();
        STA_CHECK(s_sta_node_mutex != NULL, "sensor_node xSemaphoreCreateMutex failed", ESP_FAIL);
        s_if_init = true;
        return ESP_OK;
    }
    ESP_LOGI(TAG, "http server already initialized");
    return ESP_FAIL;
}

