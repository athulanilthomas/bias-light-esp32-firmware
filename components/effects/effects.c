#include "effects.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "effects";

#define NUM_LEDS CONFIG_LED_COUNT
#define FRAME_TICKS ((pdMS_TO_TICKS(1000 / CONFIG_RENDER_FPS)) > 0 ? pdMS_TO_TICKS(1000 / CONFIG_RENDER_FPS) : 1)

/* Re-send an unchanged frame this often so a glitched strip self-heals. */
#define KEEPALIVE_MS 1000
#define BREATHE_PERIOD_MS 6000
#define RAINBOW_PERIOD_MS 30000
/* Rough 2700K look on WS2812B (their green die is strong, so G/B are pulled down). */
#define WARM_R 255
#define WARM_G 147
#define WARM_B 41

static const char *const MODE_NAMES[EFFECT_COUNT] = {
    [EFFECT_SOLID] = "solid",
    [EFFECT_WARM_WHITE] = "warm_white",
    [EFFECT_BREATHE] = "breathe",
    [EFFECT_RAINBOW] = "rainbow",
};

static led_strip_handle_t s_strip;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static light_state_t s_state;
static effects_listener_t s_listeners[EFFECTS_MAX_LISTENERS];
static size_t s_num_listeners;

static uint8_t s_gamma[256];
/* 0..256 intensity curve for breathe, one full period over 256 entries. */
static uint16_t s_breathe[256];

static uint8_t s_frame[NUM_LEDS * 3];
static uint8_t s_prev[NUM_LEDS * 3];

/* ---------- state helpers ---------- */

light_state_t effects_default_state(void)
{
    light_state_t s = {
        .on = true,
        .mode = EFFECT_WARM_WHITE,
        .r = WARM_R,
        .g = WARM_G,
        .b = WARM_B,
        .brightness = CONFIG_DEFAULT_BRIGHTNESS_PCT,
    };
    return s;
}

void effects_sanitize(light_state_t *s)
{
    if ((unsigned)s->mode >= EFFECT_COUNT) {
        s->mode = EFFECT_SOLID;
    }
    if (s->brightness > 100) {
        s->brightness = 100;
    }
    s->on = s->on ? true : false;
}

const char *effects_mode_name(effect_mode_t mode)
{
    return ((unsigned)mode < EFFECT_COUNT) ? MODE_NAMES[mode] : "solid";
}

bool effects_mode_from_name(const char *name, effect_mode_t *out)
{
    if (!name) {
        return false;
    }
    for (int i = 0; i < EFFECT_COUNT; i++) {
        if (strcmp(name, MODE_NAMES[i]) == 0) {
            *out = (effect_mode_t)i;
            return true;
        }
    }
    return false;
}

void effects_get_state(light_state_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_state;
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t effects_add_listener(effects_listener_t cb)
{
    if (!cb || s_num_listeners >= EFFECTS_MAX_LISTENERS) {
        return ESP_ERR_INVALID_ARG;
    }
    s_listeners[s_num_listeners++] = cb;
    return ESP_OK;
}

void effects_apply_patch(const light_patch_t *p)
{
    light_state_t snapshot;

    taskENTER_CRITICAL(&s_lock);
    if (p->has_on) {
        s_state.on = p->on;
    }
    if (p->has_mode && (unsigned)p->mode < EFFECT_COUNT) {
        s_state.mode = p->mode;
    }
    if (p->has_color) {
        s_state.r = p->r;
        s_state.g = p->g;
        s_state.b = p->b;
    }
    if (p->has_brightness) {
        s_state.brightness = p->brightness > 100 ? 100 : p->brightness;
    }
    snapshot = s_state;
    taskEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "state: on=%d mode=%s color=(%u,%u,%u) brightness=%u", snapshot.on,
             effects_mode_name(snapshot.mode), snapshot.r, snapshot.g, snapshot.b, snapshot.brightness);

    for (size_t i = 0; i < s_num_listeners; i++) {
        s_listeners[i](&snapshot);
    }
}

/* ---------- fixed-point colour math ---------- */

static void build_tables(void)
{
    for (int i = 0; i < 256; i++) {
        s_gamma[i] = (uint8_t)lroundf(255.0f * powf(i / 255.0f, 2.2f));
        float c = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / 256.0f));
        s_breathe[i] = (uint16_t)lroundf(256.0f * (0.04f + 0.96f * c));
    }
}

/* hue is a 16-bit angle (0..65535 = 0..360 deg); full saturation and value. */
static void hue_to_rgb(uint16_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint32_t x = (uint32_t)hue * 6;
    uint32_t region = x >> 16;
    uint8_t f = (x >> 8) & 0xFF;
    uint8_t q = 255 - f;

    switch (region) {
    case 0: *r = 255; *g = f;   *b = 0;   break;
    case 1: *r = q;   *g = 255; *b = 0;   break;
    case 2: *r = 0;   *g = 255; *b = f;   break;
    case 3: *r = 0;   *g = q;   *b = 255; break;
    case 4: *r = f;   *g = 0;   *b = 255; break;
    default: *r = 255; *g = 0;  *b = q;   break;
    }
}

/* gamma first (perceptual -> linear light), then scale by 0..256 intensity. */
static inline uint8_t shade(uint8_t c, uint32_t intensity)
{
    return (uint8_t)((s_gamma[c] * intensity) >> 8);
}

static void fill_solid(uint8_t r, uint8_t g, uint8_t b, uint32_t intensity)
{
    uint8_t rr = shade(r, intensity), gg = shade(g, intensity), bb = shade(b, intensity);
    for (int i = 0; i < NUM_LEDS; i++) {
        s_frame[i * 3 + 0] = rr;
        s_frame[i * 3 + 1] = gg;
        s_frame[i * 3 + 2] = bb;
    }
}

static void render_frame(const light_state_t *st, uint32_t now_ms, uint32_t scale)
{
    switch (st->mode) {
    case EFFECT_WARM_WHITE:
        fill_solid(WARM_R, WARM_G, WARM_B, scale);
        break;
    case EFFECT_BREATHE: {
        uint32_t phase = (now_ms % BREATHE_PERIOD_MS) * 256 / BREATHE_PERIOD_MS;
        fill_solid(st->r, st->g, st->b, (scale * s_breathe[phase]) >> 8);
        break;
    }
    case EFFECT_RAINBOW: {
        uint32_t base = (uint32_t)(((uint64_t)(now_ms % RAINBOW_PERIOD_MS) << 16) / RAINBOW_PERIOD_MS);
        for (int i = 0; i < NUM_LEDS; i++) {
            uint16_t hue = (uint16_t)(base + ((uint32_t)i << 16) / NUM_LEDS);
            uint8_t r, g, b;
            hue_to_rgb(hue, &r, &g, &b);
            s_frame[i * 3 + 0] = shade(r, scale);
            s_frame[i * 3 + 1] = shade(g, scale);
            s_frame[i * 3 + 2] = shade(b, scale);
        }
        break;
    }
    case EFFECT_SOLID:
    default:
        fill_solid(st->r, st->g, st->b, scale);
        break;
    }
}

/* Scale applied to linear light: 0..256 where 256 would be 100% output. */
static uint32_t target_scale(const light_state_t *st)
{
    if (!st->on) {
        return 0;
    }
    uint32_t scale = ((uint32_t)st->brightness * CONFIG_MAX_BRIGHTNESS_PCT * 256) / 10000;
    uint32_t cap = (CONFIG_MAX_BRIGHTNESS_PCT * 256) / 100;
    return scale > cap ? cap : scale;
}

/* ---------- render task ---------- */

static void push_frame(void)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        led_strip_set_pixel(s_strip, i, s_frame[i * 3], s_frame[i * 3 + 1], s_frame[i * 3 + 2]);
    }
    esp_err_t err = led_strip_refresh(s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "refresh failed: %s", esp_err_to_name(err));
    }
}

static void render_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    uint32_t cur_scale = 0;
    uint32_t last_push_ms = 0;
    bool first = true;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        light_state_t st;
        effects_get_state(&st);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        /* Slew brightness so on/off and slider moves fade instead of popping. */
        uint32_t target = target_scale(&st);
        if (cur_scale != target) {
            int32_t d = (int32_t)target - (int32_t)cur_scale;
            int32_t step = d / 4;
            if (step == 0) {
                step = d > 0 ? 1 : -1;
            }
            cur_scale = (uint32_t)((int32_t)cur_scale + step);
        }

        render_frame(&st, now_ms, cur_scale);

        if (first || memcmp(s_frame, s_prev, sizeof(s_frame)) != 0 || (now_ms - last_push_ms) >= KEEPALIVE_MS) {
            push_frame();
            memcpy(s_prev, s_frame, sizeof(s_frame));
            last_push_ms = now_ms;
            first = false;
        }

        esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, FRAME_TICKS);
    }
}

esp_err_t effects_init(const light_state_t *initial)
{
    build_tables();

    s_state = initial ? *initial : effects_default_state();
    effects_sanitize(&s_state);

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_LED_GPIO,
        .max_leds = NUM_LEDS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        /* Several RMT blocks => fewer refill interrupts => less Wi-Fi induced glitching. */
        .mem_block_symbols = 192,
        .flags.with_dma = false,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip), TAG, "strip init failed");
    led_strip_clear(s_strip);

    /* Core 1 keeps the render loop away from the Wi-Fi stack on core 0. */
    BaseType_t ok = xTaskCreatePinnedToCore(render_task, "render", 4096, NULL, 5, NULL, 1);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "render task create failed");

    ESP_LOGI(TAG, "strip ready: %d LEDs on GPIO %d, cap %d%%, %d fps", NUM_LEDS, CONFIG_LED_GPIO,
             CONFIG_MAX_BRIGHTNESS_PCT, CONFIG_RENDER_FPS);
    return ESP_OK;
}
