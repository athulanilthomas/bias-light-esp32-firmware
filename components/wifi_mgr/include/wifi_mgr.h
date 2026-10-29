#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start Wi-Fi in station mode using the Kconfig credentials and advertise the
 * mDNS hostname. Non-blocking: connection and reconnection (with back-off)
 * happen in the background.
 */
esp_err_t wifi_mgr_start(void);

/**
 * (Re)apply the mDNS hostname and the _http._tcp service. Initialises mDNS if
 * needed. Used after another component (HomeKit) has re-initialised mDNS.
 */
esp_err_t wifi_mgr_mdns_start(void);

#ifdef __cplusplus
}
#endif
