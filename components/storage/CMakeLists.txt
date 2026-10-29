idf_component_register(
    SRCS "storage.c"
    INCLUDE_DIRS "include"
    REQUIRES effects
    PRIV_REQUIRES nvs_flash log freertos
)
