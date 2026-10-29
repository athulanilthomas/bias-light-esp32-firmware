#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EFFECT_SOLID = 0,
    EFFECT_WARM_WHITE,
    EFFECT_BREATHE,
    EFFECT_RAINBOW,
    EFFECT_COUNT,
} effect_mode_t;

/** Complete user-visible light state. brightness is 0..100 and is scaled onto 0..CONFIG_MAX_BRIGHTNESS_PCT. */
typedef struct {
    bool on;
    effect_mode_t mode;
    uint8_t r, g, b;
    uint8_t brightness;
} light_state_t;

/** Partial update: only fields whose has_* flag is set are applied. */
typedef struct {
    bool has_on, has_mode, has_color, has_brightness;
    bool on;
    effect_mode_t mode;
    uint8_t r, g, b;
    uint8_t brightness;
} light_patch_t;

/** Called (from the caller's context, outside any lock) after the state changed. */
typedef void (*effects_listener_t)(const light_state_t *state);

/** Returns the factory default state (from Kconfig). */
light_state_t effects_default_state(void);

/** Clamp every field of a state into its valid range. */
void effects_sanitize(light_state_t *state);

/** Start the strip and the render task using the given initial state (sanitised). */
esp_err_t effects_init(const light_state_t *initial);

void effects_get_state(light_state_t *out);

/** Atomically apply a patch (values are clamped), then notify the listener. */
void effects_apply_patch(const light_patch_t *patch);

/** Register a single change listener (e.g. persistence). Pass NULL to remove. */
void effects_set_listener(effects_listener_t cb);

const char *effects_mode_name(effect_mode_t mode);

/** Returns false if name is not a known mode. */
bool effects_mode_from_name(const char *name, effect_mode_t *out);

#ifdef __cplusplus
}
#endif
