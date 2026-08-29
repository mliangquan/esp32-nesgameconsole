#include <esp_http_server.h>
#include <dirent.h>
#include <stdio.h>
#include <cJSON.h>
#include <ctype.h>

// static const char webui_html[];
#include "webui.html.h"
#include "esp_log.h"

static httpd_handle_t server;
static char *http_buffer;

static char *urldecode(const char *str)
{
    char *new_string = strdup(str);
    char *ptr = new_string;
    while (ptr[0] && ptr[1] && ptr[2])
    {
        if (ptr[0] == '%' && isxdigit((unsigned char)ptr[1]) && isxdigit((unsigned char)ptr[2]))
        {
            char hex[] = {ptr[1], ptr[2], 0};
            *ptr = strtol(hex, NULL, 16);
            memmove(ptr + 1, ptr + 3, strlen(ptr + 3) + 1);
        }
        ptr++;
    }
    return new_string;
}

static int add_file(const rg_scandir_t *entry, void *arg)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", entry->basename);
    cJSON_AddNumberToObject(obj, "size", entry->size);
    cJSON_AddNumberToObject(obj, "mtime", entry->mtime);
    cJSON_AddBoolToObject(obj, "is_dir", entry->is_dir);
    cJSON_AddItemToArray((cJSON *)arg, obj);
    return RG_SCANDIR_CONTINUE;
}

static esp_err_t http_api_handler(httpd_req_t *req)
{
    ESP_LOGI(__FILE__, "new uri req: %s", req->uri);

    char http_buffer[1024] = {0};
    bool success = false;
    FILE *fp;

    if (httpd_req_recv(req, http_buffer, sizeof(http_buffer)) < 2)
    {
        ESP_LOGE(__FILE__, "req recv length < 2");
        return ESP_FAIL;
    }

    cJSON *content = cJSON_Parse(http_buffer);
    if (!content){
        ESP_LOGE(__FILE__, "JSON Parse faild.");
        return ESP_FAIL;
    }

    const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(content, "cmd")) ?: "-";
    const char *arg1 = cJSON_GetStringValue(cJSON_GetObjectItem(content, "arg1")) ?: "";
    const char *arg2 = cJSON_GetStringValue(cJSON_GetObjectItem(content, "arg2")) ?: "";

    cJSON *response = cJSON_CreateObject();

    if (strcmp(cmd, "list") == 0)
    {
        cJSON *array = cJSON_AddArrayToObject(response, "files");
        
        size_t total = 0, used = 0;
        if(!rg_storage_get_storage_info(&total, &used))
        {
            ESP_LOGE(__FILE__, "Storage info get failed.");
            return ESP_FAIL;
        }

        size_t freespace = total - used;
        cJSON_AddNumberToObject(response, "freespace", freespace);
        cJSON_AddNumberToObject(response, "totalspace", total);

        success = array && rg_storage_scandir(arg1, add_file, array, RG_SCANDIR_SORT | RG_SCANDIR_STAT);
    }
    else if (strcmp(cmd, "rename") == 0)
    {
        success = rename(arg1, arg2) == 0;
    }
    else if (strcmp(cmd, "delete") == 0)
    {
        success = rg_storage_delete(arg1);
    }
    else if (strcmp(cmd, "mkdir") == 0)
    {
        success = rg_storage_mkdir(arg1);
    }
    else if (strcmp(cmd, "touch") == 0)
    {
        success = (fp = fopen(arg1, "wb")) && fclose(fp) == 0;
    }

    cJSON_AddBoolToObject(response, "success", success);

    char *response_text = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response_text);
    free(response_text);

    cJSON_Delete(response);
    cJSON_Delete(content);

    return ESP_OK;
}

static esp_err_t http_upload_handler(httpd_req_t *req)
{
    char *filename = urldecode(req->uri);
    bool success = false;
    const char* errmsg = "ERROR";

    ESP_LOGI(__FILE__, "Receiving file: %s, size: %d", filename, req->content_len);

    size_t freespace = rg_storage_get_free_space(RG_STORAGE_ROOT);

    if(freespace < req->content_len)
    {
        errmsg = "No more free space.";
        ESP_LOGE(__FILE__, "no more space, request: %d, free: %d", req->content_len, freespace);
        goto _done;
    }

    ESP_LOGI(__FILE__, "Free space: %d", freespace);

    vTaskDelay(100);

    FILE *fp = fopen(filename, "wb");
    if (!fp)
    {
        errmsg = "Open file failed.";
        goto _done;
    }

    size_t received = 0;

    while (received < req->content_len)
    {
        int length = httpd_req_recv(req, http_buffer, 0x8000);
        if (length <= 0)
            break;
        if (!fwrite(http_buffer, length, 1, fp))
        {
            ESP_LOGE(__FILE__, "Write failure at %d bytes", received);
            break;
        }
        received += length;
        vPortYield();
    }

    fclose(fp);

    ESP_LOGI(__FILE__, "Received %d/%d bytes", received, req->content_len);
    success = received == req->content_len;

_done:
    if (!success)
    {
        ESP_LOGE(__FILE__, "File receive error!");
        httpd_resp_sendstr(req, errmsg);
        remove(filename);
        free(filename);
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "OK");
    free(filename);
    return ESP_OK;
}

static esp_err_t http_download_handler(httpd_req_t *req)
{
    char *filename = urldecode(req->uri);
    FILE *fp;

    ESP_LOGI(__FILE__, "Serving file: %s", filename);

    // gui.http_lock = true;

    if ((fp = fopen(filename, "rb")))
    {
        if (rg_extension_match(filename, "json log txt"))
            httpd_resp_set_type(req, "text/plain");
        else if (rg_extension_match(filename, "png") == 0)
            httpd_resp_set_type(req, "image/png");
        else if (rg_extension_match(filename, "jpg") == 0)
            httpd_resp_set_type(req, "image/jpg");
        else
            httpd_resp_set_type(req, "application/binary");

        for (size_t len; (len = fread(http_buffer, 1, 0x8000, fp));)
        {
            httpd_resp_send_chunk(req, http_buffer, len);
            vPortYield();
        }

        httpd_resp_send_chunk(req, NULL, 0);
        fclose(fp);
    }
    else
    {
        httpd_resp_send_404(req);
    }
    free(filename);

    // gui.http_lock = false;

    return ESP_OK;
}

static esp_err_t http_get_handler(httpd_req_t *req)
{
    ESP_LOGI(__FILE__, "new uri req: %s", req->uri);
    httpd_resp_sendstr(req, webui_html);
    return ESP_OK;
}

void webui_stop(void)
{
    if (!server) // Already stopped
        return;

    httpd_stop(server);
    server = NULL;

    free(http_buffer);
    http_buffer = NULL;
}

void webui_start(void)
{
    if (server) // Already started
        return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(__FILE__, "Failed to start webserver: 0x%03X", err);
        return;
    }

    http_buffer = malloc(0x10000);

    if(http_buffer == NULL){
        ESP_LOGE(__FILE__, "http buffer malloc failed.");
        abort();
    }

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = http_get_handler,
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/api",
        .method    = HTTP_POST,
        .handler   = http_api_handler,
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/*",
        .method    = HTTP_GET,
        .handler   = http_download_handler,
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/*",
        .method    = HTTP_PUT,
        .handler   = http_upload_handler,
    });

    // RG_ASSERT(http_buffer && server, "Something went wrong starting server");
    ESP_LOGI(__FILE__, "Web server started");
}
