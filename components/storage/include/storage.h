#pragma once

#include "effects.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load the saved state (sanitised). Returns ESP_ERR_NOT_FOUND if none/invalid; *out is then untouched. */
esp_err_t storage_load(light_state_t *out);

/**
 * Start the debounced writer. Afterwards, storage_on_state_changed() (usable
 * directly as an effects_listener_t) schedules a save. NVS must be initialised.
 */
esp_err_t storage_start(void);

void storage_on_state_changed(const light_state_t *state);

#ifdef __cplusplus
}
#endif
