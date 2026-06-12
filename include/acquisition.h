#ifndef ACQUISITION_H
#define ACQUISITION_H

#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"
#include "magnetometer.h"

typedef struct {
    magnetometer_sample_t sensors[SENSOR_COUNT];
} acquisition_frame_t;

typedef struct {
    acquisition_frame_t frames[ACQ_MAX_SAMPLES];
    size_t frame_count;
} acquisition_buffer_t;

esp_err_t acquisition_capture(acquisition_buffer_t *buffer, uint32_t duration_ms);

#endif
