idf_component_register(
    SRCS "http_api.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "index.html"
    REQUIRES effects
    PRIV_REQUIRES esp_http_server json log
)
