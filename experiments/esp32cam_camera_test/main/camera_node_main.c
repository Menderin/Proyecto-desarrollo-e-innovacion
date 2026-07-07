#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "camera_service.h"
#include "detector_link.h"
#include "telegram_service.h"
#include "wifi_service.h"

static const char *TAG = "CAM_NODE";

static void maybe_send_boot_test_photo(void)
{
#if CONFIG_CAMERA_TELEGRAM_TEST_ON_BOOT
    ESP_LOGW(TAG, "Prueba Telegram activa: se enviara una foto al arrancar");

    if (!wifi_service_wait_connected(pdMS_TO_TICKS(20000))) {
        ESP_LOGE(TAG, "Prueba Telegram cancelada: Wi-Fi no conectado");
        return;
    }

    const uint32_t test_id = 9999;
    esp_err_t result = camera_service_capture(test_id, "BOOT_TEST");
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Prueba Telegram: fallo captura: %s", esp_err_to_name(result));
        return;
    }

    telegram_alert_t alert = {
        .p90_raw = 0,
        .threshold_raw = 0,
    };
    result = camera_service_take_pending(test_id, &alert.photo);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Prueba Telegram: no se pudo tomar foto pendiente: %s",
                 esp_err_to_name(result));
        return;
    }

    result = telegram_service_enqueue(&alert);
    if (result == ESP_OK) {
        ESP_LOGW(TAG, "Prueba Telegram: foto encolada, espera envio al grupo");
    } else {
        ESP_LOGE(TAG, "Prueba Telegram: no se pudo encolar: %s", esp_err_to_name(result));
        camera_service_release_photo(&alert.photo);
    }
#else
    ESP_LOGI(TAG, "Prueba Telegram al arrancar deshabilitada");
#endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando nodo ESP32-CAM");
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    ESP_ERROR_CHECK(camera_service_init());
    ESP_ERROR_CHECK(wifi_service_init());
    ESP_ERROR_CHECK(telegram_service_init());
    maybe_send_boot_test_photo();
    ESP_ERROR_CHECK(detector_link_init());
    ESP_LOGI(TAG, "Camara lista; esperando START/SAFE/ALERT");
    detector_link_run();
}
