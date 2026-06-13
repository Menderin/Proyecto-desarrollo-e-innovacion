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
    // Verificar que el multiplexor TCA9548A responde en el bus principal
    // Los HMC5883L estan detras del mux y NO son visibles directamente
    esp_err_t probe_res = i2c_bus_probe(TCA9548A_ADDR, 100);
    if (probe_res == ESP_OK) {
        ESP_LOGI(TAG, "TCA9548A encontrado en 0x%02X — bus I2C OK", TCA9548A_ADDR);
    } else {
        ESP_LOGE(TAG, "TCA9548A NO detectado (0x%02X) err=%s — verificar SDA/SCL y pull-ups",
                 TCA9548A_ADDR, esp_err_to_name(probe_res));
    }

    ESP_ERROR_CHECK(ir_sensor_init());
    ESP_LOGI(TAG, "GPIOs IR inicializados correctamente");

    // Escaneo de diagnostico: mostrar dispositivos en cada canal del mux
    ESP_LOGI(TAG, "--- Escaneo I2C de diagnostico ---");
    tca9548a_scan_channels(0, SENSOR_COUNT);
    ESP_LOGI(TAG, "--- Fin escaneo ---");

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
            static acquisition_buffer_t buffer;
            esp_err_t acq_result = acquisition_capture(&buffer, 500);
            if (acq_result != ESP_OK) {
                ESP_LOGE(TAG, "Fallo en adquisicion: %s", esp_err_to_name(acq_result));
            } else {
                classifier_result_t result = classifier_run(&buffer);
                ESP_LOGI(TAG, "Resultado clasificacion: %s",
                         classifier_result_to_string(result));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
