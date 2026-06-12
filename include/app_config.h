#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2c.h"

#define PIN_IR_1        GPIO_NUM_13
#define PIN_IR_2        GPIO_NUM_12

#define I2C_MASTER_SDA  GPIO_NUM_21
#define I2C_MASTER_SCL  GPIO_NUM_15
#define I2C_PORT        I2C_NUM_0
#define I2C_FREQ_HZ     100000

#define TCA9548A_ADDR   0x70
#define SENSOR_COUNT    2
#define MAGNETOMETER_I2C_ADDR 0x1E

#define ACQ_MAX_SAMPLES 64
#define ACQ_SAMPLE_MS   20

#endif
