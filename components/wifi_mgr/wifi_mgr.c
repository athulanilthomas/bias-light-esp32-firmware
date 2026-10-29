#include "wifi_mgr.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_mgr";

#define RETRY_MIN_MS 500

static esp_timer_handle_t s_retry_timer;
static uint32_t s_retry_ms = RETRY_MIN_MS;

static void retry_cb(void *arg)
{
    (void)arg;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "disconnected, retrying in %u ms", (unsigned)s_retry_ms);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)s_retry_ms * 1000);
        s_retry_ms *= 2;
        if (s_retry_ms > CONFIG_WIFI_RETRY_MAX_DELAY_MS) {
            s_retry_ms = CONFIG_WIFI_RETRY_MAX_DELAY_MS;
        }
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "got IP " IPSTR " (http://%s.local/)", IP2STR(&ev->ip_info.ip), CONFIG_MDNS_HOSTNAME);
    s_retry_ms = RETRY_MIN_MS;
}

static esp_err_t start_mdns(esp_netif_t *sta)
{
    esp_err_t err = esp_netif_set_hostname(sta, CONFIG_MDNS_HOSTNAME);
    if (err != ESP_OK) {
        return err;
    }
    err = mdns_init();
    if (err != ESP_OK) {
        return err;
    }
    mdns_hostname_set(CONFIG_MDNS_HOSTNAME);
    mdns_instance_name_set("TV Bias Light");
    return mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}

esp_err_t wifi_mgr_start(void)
{
    if (strlen(CONFIG_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "no SSID configured (menuconfig > Bias Light: Network); Wi-Fi disabled");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();

    const esp_timer_create_args_t targs = {.callback = retry_cb, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL));

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, CONFIG_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = strlen(CONFIG_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Mains powered: trade power for request latency (<200 ms target). */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    err = start_mdns(sta);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS start failed: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "connecting to \"%s\"", CONFIG_WIFI_SSID);
    return ESP_OK;
}
