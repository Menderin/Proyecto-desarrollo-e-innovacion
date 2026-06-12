#include "magnetometer.h"

#include "esp_err.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "i2c_bus.h"
#include "tca9548a.h"

#define HMC5883L_REG_CONFIG_A 0x00
#define HMC5883L_REG_CONFIG_B 0x01
#define HMC5883L_REG_MODE     0x02
#define HMC5883L_REG_DATA_X_MSB 0x03

#define HMC5883L_CONFIG_A_75HZ 0x18
#define HMC5883L_GAIN_1_3_GA   0x20
#define HMC5883L_MODE_CONTINUOUS 0x00

#define HMC5883L_LSB_PER_GAUSS 1090.0f
#define UT_PER_GAUSS           100.0f

static esp_err_t magnetometer_write_register(uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};

    return i2c_master_write_to_device(
        I2C_PORT,
        address,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
}

esp_err_t magnetometer_init(uint8_t channel, uint8_t address)
{
    esp_err_t result = tca9548a_select_channel(channel);
    if (result != ESP_OK) {
        return result;
    }

    result = i2c_bus_probe(address, 50);
    if (result != ESP_OK) {
        return result;
    }

    result = magnetometer_write_register(address, HMC5883L_REG_CONFIG_A, HMC5883L_CONFIG_A_75HZ);
    if (result != ESP_OK) {
        return result;
    }

    result = magnetometer_write_register(address, HMC5883L_REG_CONFIG_B, HMC5883L_GAIN_1_3_GA);
    if (result != ESP_OK) {
        return result;
    }

    result = magnetometer_write_register(address, HMC5883L_REG_MODE, HMC5883L_MODE_CONTINUOUS);
    if (result != ESP_OK) {
        return result;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t magnetometer_read_axes(uint8_t channel, uint8_t address, magnetometer_axes_t *axes)
{
    if (axes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = tca9548a_select_channel(channel);
    if (result != ESP_OK) {
        return result;
    }

    uint8_t reg = HMC5883L_REG_DATA_X_MSB;
    uint8_t data[6] = {0};

    result = i2c_master_write_read_device(
        I2C_PORT,
        address,
        &reg,
        sizeof(reg),
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
    if (result != ESP_OK) {
        return result;
    }

    axes->x = (int16_t)((data[0] << 8) | data[1]);
    axes->z = (int16_t)((data[2] << 8) | data[3]);
    axes->y = (int16_t)((data[4] << 8) | data[5]);

    return ESP_OK;
}

float magnetometer_raw_to_ut(int16_t raw)
{
    return ((float)raw * UT_PER_GAUSS) / HMC5883L_LSB_PER_GAUSS;
}
