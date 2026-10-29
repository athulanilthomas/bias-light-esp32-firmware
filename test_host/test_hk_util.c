#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hk_util.h"

static uint32_t rnd(void)
{
    return (uint32_t)rand();
}

int main(void)
{
    assert(hk_setup_code_valid("482-19-736"));
    assert(!hk_setup_code_valid("111-22-333"));
    assert(!hk_setup_code_valid("123-45-678"));
    assert(!hk_setup_code_valid("000-00-000"));
    assert(!hk_setup_code_valid("555-55-555"));
    assert(!hk_setup_code_valid("48219736"));
    assert(!hk_setup_code_valid("482-19-73a"));
    assert(!hk_setup_code_valid("482-19-7366"));
    assert(!hk_setup_code_valid(""));
    assert(!hk_setup_code_valid(NULL));

    char code[HK_SETUP_CODE_LEN + 1], id[HK_SETUP_ID_LEN + 1];
    for (int i = 0; i < 20000; i++) {
        hk_setup_code_generate(code, rnd);
        assert(hk_setup_code_valid(code));
        hk_setup_id_generate(id, rnd);
        assert(strlen(id) == HK_SETUP_ID_LEN);
        for (int k = 0; k < HK_SETUP_ID_LEN; k++) {
            assert((id[k] >= 'A' && id[k] <= 'Z') || (id[k] >= '0' && id[k] <= '9'));
        }
    }

    uint8_t r, g, b;
    hk_hsv_to_rgb(0, 100, &r, &g, &b);   assert(r == 255 && g == 0 && b == 0);
    hk_hsv_to_rgb(120, 100, &r, &g, &b); assert(r == 0 && g == 255 && b == 0);
    hk_hsv_to_rgb(240, 100, &r, &g, &b); assert(r == 0 && g == 0 && b == 255);
    hk_hsv_to_rgb(300, 0, &r, &g, &b);   assert(r == 255 && g == 255 && b == 255);
    hk_hsv_to_rgb(360, 100, &r, &g, &b); assert(r == 255 && g == 0 && b == 0);
    hk_hsv_to_rgb(-5, 500, &r, &g, &b);  assert(r == 255 && g == 0 && b == 0);
    hk_hsv_to_rgb(1e30f, 50, &r, &g, &b);

    /* Round trip stays within 1 LSB for full-value colours. */
    for (int h = 0; h < 360; h += 7) {
        for (int s = 10; s <= 100; s += 15) {
            float h2, s2;
            uint8_t r2, g2, b2;
            hk_hsv_to_rgb(h, s, &r, &g, &b);
            hk_rgb_to_hsv(r, g, b, &h2, &s2);
            hk_hsv_to_rgb(h2, s2, &r2, &g2, &b2);
            assert(abs(r - r2) <= 1 && abs(g - g2) <= 1 && abs(b - b2) <= 1);
        }
    }
    puts("hk_util host tests passed");
    return 0;
}
