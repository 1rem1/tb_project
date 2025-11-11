idf_component_register(
    SRCS "app_main.c"
    REQUIRES wifi_service nvs_storage tb_client cjson esp_netif esp_event
    INCLUDE_DIRS "."
)
