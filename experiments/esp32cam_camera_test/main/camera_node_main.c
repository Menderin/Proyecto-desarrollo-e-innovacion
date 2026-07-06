#include "esp_err.h"
#include "esp_log.h"

#include "camera_service.h"
#include "detector_link.h"

static const char *TAG = "CAM_NODE";

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando nodo ESP32-CAM fase 1");
    ESP_ERROR_CHECK(camera_service_init());
    ESP_ERROR_CHECK(detector_link_init());
    ESP_LOGI(TAG, "Camara lista; esperando START/SAFE/ALERT");
    detector_link_run();
}

