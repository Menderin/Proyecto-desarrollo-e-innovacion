#ifndef MAGNETOMETER_H
#define MAGNETOMETER_H

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} magnetometer_axes_t;

typedef struct {
    uint8_t channel;
    uint8_t address;
    magnetometer_axes_t axes;
} magnetometer_sample_t;

esp_err_t magnetometer_init(uint8_t channel, uint8_t address);
esp_err_t magnetometer_read_axes(uint8_t channel, uint8_t address, magnetometer_axes_t *axes);
float magnetometer_raw_to_ut(int16_t raw);

#endif
