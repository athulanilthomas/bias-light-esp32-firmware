#include "effects.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    light_state_t state = effects_default_state();
    ESP_ERROR_CHECK(effects_init(&state));
    ESP_LOGI(TAG, "tvlight started");
}
