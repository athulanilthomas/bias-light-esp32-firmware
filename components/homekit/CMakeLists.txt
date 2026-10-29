idf_component_register(
    SRCS "homekit.c" "hk_util.c"
    INCLUDE_DIRS "include"
    REQUIRES effects
    PRIV_REQUIRES esp_hap_core esp_hap_apple_profiles wifi_mgr nvs_flash mdns esp_system log
)
