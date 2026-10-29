idf_component_register(
    SRCS "effects.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
    PRIV_REQUIRES esp_driver_rmt esp_driver_gpio freertos log led_strip
)
