#include "i2c_bus.h"

#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"

esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA,
        .scl_io_num = I2C_MASTER_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

esp_err_t i2c_bus_probe(uint8_t addr, uint32_t timeout_ms)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);

    esp_err_t result = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);

    return result;
}
