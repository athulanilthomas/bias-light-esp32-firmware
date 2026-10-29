idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES effects storage wifi_mgr homekit http_api nvs_flash esp_system log
)
