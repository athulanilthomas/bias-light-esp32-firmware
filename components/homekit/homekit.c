#include "homekit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hap.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>

#include "effects.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hk_util.h"
#include "mdns.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "wifi_mgr.h"

static const char *TAG = "homekit";

#define NVS_NAMESPACE "tvlight_hk"
#define NVS_KEY_CODE "code"
#define NVS_KEY_ID "id"

static hap_char_t *s_on_char, *s_bri_char, *s_hue_char, *s_sat_char;
static SemaphoreHandle_t s_lock;
/* Last hue/saturation known to HomeKit; lets us avoid rounding jitter when echoing colour back. */
static float s_hue, s_sat;

static uint32_t rnd32(void)
{
    return esp_random();
}

/* ---------- setup code / ID ---------- */

static esp_err_t nvs_get_str_or_create(nvs_handle_t h, const char *key, char *out, size_t cap, bool is_code)
{
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err == ESP_OK) {
        bool ok = is_code ? hk_setup_code_valid(out) : (strlen(out) == HK_SETUP_ID_LEN);
        if (ok) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "stored %s is invalid, regenerating", key);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    if (is_code) {
        hk_setup_code_generate(out, rnd32);
    } else {
        hk_setup_id_generate(out, rnd32);
    }
    err = nvs_set_str(h, key, out);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    return err;
}

static esp_err_t load_credentials(char *code, char *id)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    if (strlen(CONFIG_HOMEKIT_SETUP_CODE) > 0) {
        if (!hk_setup_code_valid(CONFIG_HOMEKIT_SETUP_CODE)) {
            ESP_LOGE(TAG, "CONFIG_HOMEKIT_SETUP_CODE is malformed or a well-known/trivial code; HomeKit disabled");
            nvs_close(h);
            return ESP_ERR_INVALID_ARG;
        }
        strlcpy(code, CONFIG_HOMEKIT_SETUP_CODE, HK_SETUP_CODE_LEN + 1);
    } else {
        err = nvs_get_str_or_create(h, NVS_KEY_CODE, code, HK_SETUP_CODE_LEN + 1, true);
    }
    if (err == ESP_OK) {
        err = nvs_get_str_or_create(h, NVS_KEY_ID, id, HK_SETUP_ID_LEN + 1, false);
    }
    nvs_close(h);
    return err;
}

/* ---------- HomeKit <-> effects ---------- */

static int light_identify(hap_acc_t *ha)
{
    ESP_LOGI(TAG, "identify requested");
    return HAP_SUCCESS;
}

static int lightbulb_write(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
{
    light_patch_t patch = {0};
    bool color_written = false;
    int ret = HAP_SUCCESS;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    float hue = s_hue, sat = s_sat;
    xSemaphoreGive(s_lock);

    for (int i = 0; i < count; i++) {
        hap_write_data_t *w = &write_data[i];
        const char *uuid = hap_char_get_type_uuid(w->hc);
        *(w->status) = HAP_STATUS_SUCCESS;

        if (!strcmp(uuid, HAP_CHAR_UUID_ON)) {
            patch.has_on = true;
            patch.on = w->val.b;
        } else if (!strcmp(uuid, HAP_CHAR_UUID_BRIGHTNESS)) {
            patch.has_brightness = true;
            patch.brightness = w->val.i < 0 ? 0 : (w->val.i > 100 ? 100 : (uint8_t)w->val.i);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_HUE)) {
            hue = w->val.f;
            color_written = true;
        } else if (!strcmp(uuid, HAP_CHAR_UUID_SATURATION)) {
            sat = w->val.f;
            color_written = true;
        } else {
            *(w->status) = HAP_STATUS_RES_ABSENT;
            ret = HAP_FAIL;
        }
    }

    if (color_written) {
        light_state_t cur;
        effects_get_state(&cur);
        patch.has_color = true;
        hk_hsv_to_rgb(hue, sat, &patch.r, &patch.g, &patch.b);
        /* Picking a colour from Home switches effects like warm_white/rainbow to a fixed colour. */
        if (cur.mode != EFFECT_BREATHE) {
            patch.has_mode = true;
            patch.mode = EFFECT_SOLID;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_hue = hue;
        s_sat = sat;
        xSemaphoreGive(s_lock);
    }

    effects_apply_patch(&patch);

    for (int i = 0; i < count; i++) {
        if (*(write_data[i].status) == HAP_STATUS_SUCCESS) {
            hap_char_update_val(write_data[i].hc, &write_data[i].val);
        }
    }
    return ret;
}

/* Keep HomeKit in sync when the state is changed elsewhere (web UI, restore). */
static void on_state_changed(const light_state_t *st)
{
    if (!s_on_char) {
        return;
    }
    hap_val_t v;

    if (hap_char_get_val(s_on_char)->b != st->on) {
        v.b = st->on;
        hap_char_update_val(s_on_char, &v);
    }
    if (hap_char_get_val(s_bri_char)->i != st->brightness) {
        v.i = st->brightness;
        hap_char_update_val(s_bri_char, &v);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t r, g, b;
    hk_hsv_to_rgb(s_hue, s_sat, &r, &g, &b);
    bool color_differs = (r != st->r || g != st->g || b != st->b);
    float hue = s_hue, sat = s_sat;
    if (color_differs) {
        hk_rgb_to_hsv(st->r, st->g, st->b, &hue, &sat);
        s_hue = hue;
        s_sat = sat;
    }
    xSemaphoreGive(s_lock);

    if (color_differs) {
        v.f = hue;
        hap_char_update_val(s_hue_char, &v);
        v.f = sat;
        hap_char_update_val(s_sat_char, &v);
    }
}

esp_err_t homekit_start(void)
{
    char code[HK_SETUP_CODE_LEN + 1], id[HK_SETUP_ID_LEN + 1];
    esp_err_t err = load_credentials(code, id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setup credentials unavailable: %s", esp_err_to_name(err));
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    light_state_t st;
    effects_get_state(&st);
    hk_rgb_to_hsv(st.r, st.g, st.b, &s_hue, &s_sat);

    if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) {
        ESP_LOGE(TAG, "hap_init failed");
        return ESP_FAIL;
    }

    uint8_t mac[6];
    char serial[16];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(serial, sizeof(serial), "TVL-%02X%02X%02X", mac[3], mac[4], mac[5]);

    hap_acc_cfg_t cfg = {
        .name = CONFIG_HOMEKIT_ACCESSORY_NAME,
        .manufacturer = "DIY",
        .model = "TVLight1",
        .serial_num = serial,
        .fw_rev = "1.0.0",
        .hw_rev = "1.0",
        .pv = "1.1.0",
        .identify_routine = light_identify,
        .cid = HAP_CID_LIGHTING,
    };
    hap_acc_t *acc = hap_acc_create(&cfg);
    if (!acc) {
        ESP_LOGE(TAG, "accessory create failed");
        return ESP_FAIL;
    }
    hap_acc_add_wifi_transport_service(acc, 0);

    hap_serv_t *serv = hap_serv_lightbulb_create(st.on);
    if (!serv) {
        ESP_LOGE(TAG, "lightbulb service create failed");
        return ESP_FAIL;
    }
    int ret = hap_serv_add_char(serv, hap_char_name_create(CONFIG_HOMEKIT_ACCESSORY_NAME));
    ret |= hap_serv_add_char(serv, hap_char_brightness_create(st.brightness));
    ret |= hap_serv_add_char(serv, hap_char_hue_create(s_hue));
    ret |= hap_serv_add_char(serv, hap_char_saturation_create(s_sat));
    if (ret != HAP_SUCCESS) {
        ESP_LOGE(TAG, "adding lightbulb characteristics failed");
        return ESP_FAIL;
    }
    hap_serv_set_write_cb(serv, lightbulb_write);
    hap_acc_add_serv(acc, serv);

    s_on_char = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_ON);
    s_bri_char = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_BRIGHTNESS);
    s_hue_char = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_HUE);
    s_sat_char = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_SATURATION);

    hap_add_accessory(acc);
    hap_set_setup_code(code);
    hap_set_setup_id(id);

    /* HAP initialises mDNS itself and fails if it is already running, so release ours first. */
    mdns_free();
    if (hap_start() != HAP_SUCCESS) {
        ESP_LOGE(TAG, "hap_start failed");
        wifi_mgr_mdns_start();
        return ESP_FAIL;
    }
    /* HAP sets its own mDNS hostname; restore tvlight.local and the _http service. */
    wifi_mgr_mdns_start();

    ESP_ERROR_CHECK(effects_add_listener(on_state_changed));

    char *payload = esp_hap_get_setup_payload(code, id, false, cfg.cid);
    ESP_LOGW(TAG, "HomeKit setup code: %s   (setup payload: %s)", code, payload ? payload : "n/a");
    free(payload);
    return ESP_OK;
}
