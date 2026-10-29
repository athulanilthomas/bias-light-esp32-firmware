#include "hk_util.h"

#include <math.h>
#include <string.h>

static const char *const BLOCKED_CODES[] = {
    "000-00-000", "111-11-111", "222-22-222", "333-33-333", "444-44-444", "555-55-555",
    "666-66-666", "777-77-777", "888-88-888", "999-99-999", "123-45-678", "876-54-321",
    /* Espressif SDK example default and its documented alternative. */
    "111-22-333", "112-23-334",
};

bool hk_setup_code_valid(const char *code)
{
    if (!code || strlen(code) != HK_SETUP_CODE_LEN) {
        return false;
    }
    for (int i = 0; i < HK_SETUP_CODE_LEN; i++) {
        bool dash = (i == 3 || i == 6);
        if (dash ? code[i] != '-' : (code[i] < '0' || code[i] > '9')) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(BLOCKED_CODES) / sizeof(BLOCKED_CODES[0]); i++) {
        if (strcmp(code, BLOCKED_CODES[i]) == 0) {
            return false;
        }
    }
    return true;
}

void hk_setup_code_generate(char *out, uint32_t (*rnd)(void))
{
    do {
        for (int i = 0; i < HK_SETUP_CODE_LEN; i++) {
            out[i] = (i == 3 || i == 6) ? '-' : (char)('0' + rnd() % 10);
        }
        out[HK_SETUP_CODE_LEN] = '\0';
    } while (!hk_setup_code_valid(out));
}

void hk_setup_id_generate(char *out, uint32_t (*rnd)(void))
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < HK_SETUP_ID_LEN; i++) {
        out[i] = ALPHABET[rnd() % (sizeof(ALPHABET) - 1)];
    }
    out[HK_SETUP_ID_LEN] = '\0';
}

static uint8_t to_byte(float v)
{
    int x = (int)lroundf(v * 255.0f);
    return (uint8_t)(x < 0 ? 0 : (x > 255 ? 255 : x));
}

void hk_hsv_to_rgb(float hue, float sat, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!(hue >= 0.0f)) {
        hue = 0.0f;
    }
    if (hue > 360.0f) {
        hue = 360.0f;
    }
    if (!(sat >= 0.0f)) {
        sat = 0.0f;
    }
    if (sat > 100.0f) {
        sat = 100.0f;
    }
    float s = sat / 100.0f;
    float h = hue / 60.0f;
    int region = (int)h;
    float f = h - region;
    float p = 1.0f - s, q = 1.0f - s * f, t = 1.0f - s * (1.0f - f);
    float rf, gf, bf;
    switch (region % 6) {
    case 0: rf = 1; gf = t; bf = p; break;
    case 1: rf = q; gf = 1; bf = p; break;
    case 2: rf = p; gf = 1; bf = t; break;
    case 3: rf = p; gf = q; bf = 1; break;
    case 4: rf = t; gf = p; bf = 1; break;
    default: rf = 1; gf = p; bf = q; break;
    }
    *r = to_byte(rf);
    *g = to_byte(gf);
    *b = to_byte(bf);
}

void hk_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float *hue, float *sat)
{
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float max = fmaxf(rf, fmaxf(gf, bf));
    float min = fminf(rf, fminf(gf, bf));
    float d = max - min;
    float h = 0.0f;

    if (d > 0.0f) {
        if (max == rf) {
            h = 60.0f * fmodf((gf - bf) / d, 6.0f);
        } else if (max == gf) {
            h = 60.0f * ((bf - rf) / d + 2.0f);
        } else {
            h = 60.0f * ((rf - gf) / d + 4.0f);
        }
        if (h < 0.0f) {
            h += 360.0f;
        }
    }
    *hue = h;
    *sat = max > 0.0f ? (d / max) * 100.0f : 0.0f;
}
