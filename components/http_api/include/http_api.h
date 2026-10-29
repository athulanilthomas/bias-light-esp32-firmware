#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the HTTP server: GET / (UI), GET/POST /api/state. */
esp_err_t http_api_start(void);

#ifdef __cplusplus
}
#endif
