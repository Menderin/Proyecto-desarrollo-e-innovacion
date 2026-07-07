idf_component_register(
    SRCS
        "camera_node_main.c"
        "camera_service.c"
        "detector_link.c"
        "telegram_service.c"
        "wifi_service.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp32-camera
        esp_driver_uart
        esp_http_client
        esp_netif
        esp_psram
        esp_wifi
        mbedtls
        nvs_flash
)
