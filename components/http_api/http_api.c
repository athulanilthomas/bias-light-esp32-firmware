#include "http_api.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "effects.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "http_api";

#define MAX_BODY_LEN 256

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static esp_err_t send_json(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    return send_json(req, status, buf);
}

static esp_err_t send_state(httpd_req_t *req)
{
    light_state_t s;
    char buf[128];
    effects_get_state(&s);
    snprintf(buf, sizeof(buf), "{\"on\":%s,\"mode\":\"%s\",\"color\":[%u,%u,%u],\"brightness\":%u}",
             s.on ? "true" : "false", effects_mode_name(s.mode), s.r, s.g, s.b, s.brightness);
    return send_json(req, "200 OK", buf);
}

/* Clamp any JSON number (including inf/NaN) into 0..max. */
static uint8_t clamp_num(double v, uint8_t max)
{
    if (!(v > 0)) {
        return 0;
    }
    if (v >= max) {
        return max;
    }
    return (uint8_t)lround(v);
}

/* Returns NULL on success, otherwise a static error message. */
static const char *parse_patch(const cJSON *root, light_patch_t *p)
{
    memset(p, 0, sizeof(*p));
    if (!cJSON_IsObject(root)) {
        return "body must be a JSON object";
    }

    const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
    if (on) {
        if (!cJSON_IsBool(on)) {
            return "on must be boolean";
        }
        p->has_on = true;
        p->on = cJSON_IsTrue(on);
    }

    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (mode) {
        if (!cJSON_IsString(mode) || !effects_mode_from_name(mode->valuestring, &p->mode)) {
            return "unknown mode";
        }
        p->has_mode = true;
    }

    const cJSON *color = cJSON_GetObjectItemCaseSensitive(root, "color");
    if (color) {
        if (!cJSON_IsArray(color) || cJSON_GetArraySize(color) != 3) {
            return "color must be [r,g,b]";
        }
        uint8_t c[3];
        for (int i = 0; i < 3; i++) {
            const cJSON *v = cJSON_GetArrayItem(color, i);
            if (!cJSON_IsNumber(v)) {
                return "color components must be numbers";
            }
            c[i] = clamp_num(v->valuedouble, 255);
        }
        p->has_color = true;
        p->r = c[0];
        p->g = c[1];
        p->b = c[2];
    }

    const cJSON *br = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (br) {
        if (!cJSON_IsNumber(br)) {
            return "brightness must be a number";
        }
        p->has_brightness = true;
        p->brightness = clamp_num(br->valuedouble, 100);
    }
    return NULL;
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t favicon_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t state_get(httpd_req_t *req)
{
    return send_state(req);
}

static esp_err_t state_post(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0) {
        return send_error(req, "400 Bad Request", "empty body");
    }
    if (len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }

    char buf[MAX_BODY_LEN];
    size_t got = 0;
    while (got < len) {
        int n = httpd_req_recv(req, buf + got, len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return ESP_FAIL;
        }
        got += n;
    }

    cJSON *root = cJSON_ParseWithLength(buf, len);
    if (!root) {
        return send_error(req, "400 Bad Request", "invalid JSON");
    }
    light_patch_t patch;
    const char *err = parse_patch(root, &patch);
    cJSON_Delete(root);
    if (err) {
        return send_error(req, "400 Bad Request", err);
    }

    effects_apply_patch(&patch);
    return send_state(req);
}

esp_err_t http_api_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 6;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get},
        {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get},
        {.uri = "/api/state", .method = HTTP_GET, .handler = state_get},
        {.uri = "/api/state", .method = HTTP_POST, .handler = state_post},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }
    ESP_LOGI(TAG, "HTTP server listening on port %d", cfg.server_port);
    return ESP_OK;
}
