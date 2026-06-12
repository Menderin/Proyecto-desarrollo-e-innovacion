#include "ir_sensor.h"

#include "driver/gpio.h"

#include "app_config.h"

esp_err_t ir_sensor_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_IR_1) | (1ULL << PIN_IR_2),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    return gpio_config(&io_conf);
}

ir_state_t ir_sensor_read(void)
{
    ir_state_t state = {
        .ir1_detected = gpio_get_level(PIN_IR_1) == 0,
        .ir2_detected = gpio_get_level(PIN_IR_2) == 0,
    };

    return state;
}

bool ir_sensor_any_detected(ir_state_t state)
{
    return state.ir1_detected || state.ir2_detected;
}
