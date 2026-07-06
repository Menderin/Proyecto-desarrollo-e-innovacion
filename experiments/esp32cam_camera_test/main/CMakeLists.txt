idf_component_register(
    SRCS
        "camera_node_main.c"
        "camera_service.c"
        "detector_link.c"
    INCLUDE_DIRS "."
    REQUIRES esp32-camera esp_driver_uart esp_psram
)
