#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "acquisition.h"
#include "classifier.h"
#include "i2c_bus.h"
#include "ir_sensor.h"
#include "magnetometer.h"
#include "main.h"
#include "tca9548a.h"

static const char *TAG = "APP";

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando portico de clasificacion magnetica");

    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_LOGI(TAG, "I2C inicializado correctamente");
    // Probe magnetometer I2C address (0x1E) to verify bus and pull‑ups
    esp_err_t probe_res = i2c_bus_probe(MAGNETOMETER_I2C_ADDR, 100);
    if (probe_res == ESP_OK) {
        ESP_LOGI(TAG, "I2C probe OK: magnetometer found at 0x%02X", MAGNETOMETER_I2C_ADDR);
    } else {
        ESP_LOGW(TAG, "I2C probe FAILED: magnetometer not detected (0x%02X) err=%s",
                 MAGNETOMETER_I2C_ADDR, esp_err_to_name(probe_res));
    }

    ESP_ERROR_CHECK(ir_sensor_init());
    ESP_LOGI(TAG, "GPIOs IR inicializados correctamente");

    for (uint8_t channel = 0; channel < SENSOR_COUNT; channel++) {
        esp_err_t mag_result = magnetometer_init(channel, MAGNETOMETER_I2C_ADDR);
        if (mag_result == ESP_OK) {
            ESP_LOGI(TAG, "Magnetometro canal %u inicializado en 0x%02x",
                     channel,
                     MAGNETOMETER_I2C_ADDR);
        } else {
            ESP_LOGE(TAG, "No se pudo inicializar magnetometro canal %u: %s",
                     channel,
                     esp_err_to_name(mag_result));
        }
    }

    ESP_ERROR_CHECK(classifier_init());
    ESP_LOGI(TAG, "Clasificador inicializado");

    while (true) {
        ir_state_t ir_state = ir_sensor_read();

        ESP_LOGI(TAG, "IR 1 GPIO13: %s | IR 2 GPIO12: %s",
                 ir_state.ir1_detected ? "DETECTADO" : "Despejado",
                 ir_state.ir2_detected ? "DETECTADO" : "Despejado");

        if (ir_sensor_any_detected(ir_state)) {
            acquisition_buffer_t buffer;
            ESP_ERROR_CHECK(acquisition_capture(&buffer, 500));

            classifier_result_t result = classifier_run(&buffer);
            ESP_LOGI(TAG, "Resultado clasificacion: %s",
                     classifier_result_to_string(result));
        } else {
            tca9548a_scan_channels(0, SENSOR_COUNT);
        }

        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
