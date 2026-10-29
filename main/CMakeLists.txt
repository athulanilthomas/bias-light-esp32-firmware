idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES effects wifi_mgr http_api nvs_flash esp_system log
)
