#include "tca9548a.h"

#include <stdbool.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"
#include "i2c_bus.h"

static const char *TAG = "TCA9548A";

esp_err_t tca9548a_select_channel(uint8_t channel)
{
    if (channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data = 1U << channel;
    return i2c_master_write_to_device(
        I2C_PORT,
        TCA9548A_ADDR,
        &data,
        sizeof(data),
        pdMS_TO_TICKS(1000)
    );
}

esp_err_t tca9548a_disable_all(void)
{
    uint8_t data = 0;
    return i2c_master_write_to_device(
        I2C_PORT,
        TCA9548A_ADDR,
        &data,
        sizeof(data),
        pdMS_TO_TICKS(1000)
    );
}

void tca9548a_scan_channels(uint8_t first_channel, uint8_t channel_count)
{
    for (uint8_t channel = first_channel; channel < first_channel + channel_count; channel++) {
        esp_err_t select_result = tca9548a_select_channel(channel);
        if (select_result != ESP_OK) {
            ESP_LOGE(TAG, "Fallo al seleccionar canal %u: %s",
                     channel, esp_err_to_name(select_result));
            continue;
        }

        bool found = false;
        for (uint8_t addr = 1; addr < 127; addr++) {
            if (addr == TCA9548A_ADDR) {
                continue;
            }

            if (i2c_bus_probe(addr, 50) == ESP_OK) {
                ESP_LOGI(TAG, "Canal %u: dispositivo I2C en 0x%02x", channel, addr);
                found = true;
            }
        }

        if (!found) {
            ESP_LOGW(TAG, "Canal %u: sin dispositivos detectados", channel);
        }
    }
}
