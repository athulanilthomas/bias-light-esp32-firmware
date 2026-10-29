#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the HomeKit accessory (single Lightbulb: on, brightness, hue,
 * saturation) and keep it in sync with the effects state. Call after
 * wifi_mgr_start() and before http_api_start().
 */
esp_err_t homekit_start(void);

#ifdef __cplusplus
}
#endif
