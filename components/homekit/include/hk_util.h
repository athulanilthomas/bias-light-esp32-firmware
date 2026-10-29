#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HK_SETUP_CODE_LEN 10 /* "xxx-xx-xxx" */
#define HK_SETUP_ID_LEN 4

/**
 * True if code has the form "ddd-dd-ddd" and is not trivially guessable: the
 * codes Apple forbids (all-same digits, 123-45-678, 876-54-321) plus the
 * well-known sample codes shipped with the Espressif SDK.
 */
bool hk_setup_code_valid(const char *code);

/** Fill out (HK_SETUP_CODE_LEN + 1 bytes) with a random valid code using rnd() as entropy source. */
void hk_setup_code_generate(char *out, uint32_t (*rnd)(void));

/** Fill out (HK_SETUP_ID_LEN + 1 bytes) with a random 4-character [A-Z0-9] setup ID. */
void hk_setup_id_generate(char *out, uint32_t (*rnd)(void));

/** HomeKit (hue 0..360, saturation 0..100, full value) to 8-bit RGB. Inputs are clamped. */
void hk_hsv_to_rgb(float hue, float sat, uint8_t *r, uint8_t *g, uint8_t *b);

/** 8-bit RGB to HomeKit hue (0..360) and saturation (0..100). */
void hk_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float *hue, float *sat);

#ifdef __cplusplus
}
#endif
