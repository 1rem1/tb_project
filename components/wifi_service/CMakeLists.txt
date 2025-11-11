idf_component_register(SRCS "wifi_prov_mgr.c"
                       PRIV_REQUIRES esp_wifi wifi_provisioning nvs_storage
                       INCLUDE_DIRS "include")
