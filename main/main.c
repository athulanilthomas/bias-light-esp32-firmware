#include "effects.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_api.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

static void heap_monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        ESP_LOGI(TAG, "heap: free=%u min_free=%u largest_block=%u", (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        vTaskDelay(pdMS_TO_TICKS(CONFIG_HEAP_LOG_INTERVAL_S * 1000));
    }
}

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    nvs_init();

    light_state_t state = effects_default_state();
    ESP_ERROR_CHECK(effects_init(&state));

    if (wifi_mgr_start() == ESP_OK) {
        ESP_ERROR_CHECK(http_api_start());
    }

    xTaskCreate(heap_monitor_task, "heap_mon", 2048, NULL, 1, NULL);
    ESP_LOGI(TAG, "tvlight started");
}
