#include "storage.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "storage";

#define NVS_NAMESPACE "tvlight"
#define NVS_KEY "state"
#define STATE_VERSION 1

/* Fixed-layout record so the blob does not depend on compiler padding or enum width. */
typedef struct {
    uint8_t version;
    uint8_t on;
    uint8_t mode;
    uint8_t r, g, b;
    uint8_t brightness;
} stored_state_t;

static TaskHandle_t s_task;
static stored_state_t s_last_written;
static bool s_have_last;

static void pack(const light_state_t *s, stored_state_t *o)
{
    o->version = STATE_VERSION;
    o->on = s->on;
    o->mode = (uint8_t)s->mode;
    o->r = s->r;
    o->g = s->g;
    o->b = s->b;
    o->brightness = s->brightness;
}

esp_err_t storage_load(light_state_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
    }

    stored_state_t rec;
    size_t len = sizeof(rec);
    err = nvs_get_blob(h, NVS_KEY, &rec, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (len != sizeof(rec) || rec.version != STATE_VERSION || rec.mode >= EFFECT_COUNT || rec.brightness > 100) {
        ESP_LOGW(TAG, "ignoring invalid saved state");
        return ESP_ERR_NOT_FOUND;
    }

    light_state_t s = {
        .on = rec.on != 0,
        .mode = (effect_mode_t)rec.mode,
        .r = rec.r,
        .g = rec.g,
        .b = rec.b,
        .brightness = rec.brightness,
    };
    effects_sanitize(&s);
    *out = s;
    s_last_written = rec;
    s_have_last = true;
    ESP_LOGI(TAG, "restored: on=%d mode=%s color=(%u,%u,%u) brightness=%u", s.on, effects_mode_name(s.mode), s.r,
             s.g, s.b, s.brightness);
    return ESP_OK;
}

static esp_err_t save(const light_state_t *s)
{
    stored_state_t rec;
    pack(s, &rec);
    if (s_have_last && memcmp(&rec, &s_last_written, sizeof(rec)) == 0) {
        return ESP_OK;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY, &rec, sizeof(rec));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        s_last_written = rec;
        s_have_last = true;
        ESP_LOGI(TAG, "state saved");
    }
    return err;
}

static void writer_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_STORAGE_SAVE_DEBOUNCE_MS));
        /* Drop notifications that arrived while waiting; the snapshot below includes them. */
        ulTaskNotifyTake(pdTRUE, 0);

        light_state_t s;
        effects_get_state(&s);
        esp_err_t err = save(&s);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t storage_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(writer_task, "storage", 3072, NULL, 2, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void storage_on_state_changed(const light_state_t *state)
{
    (void)state;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}
