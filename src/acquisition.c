#include "acquisition.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_config.h"

static const char *TAG = "ACQ";

static const uint8_t MAGNETOMETER_CHANNELS[SENSOR_COUNT] = {0, 1};
static const uint8_t MAGNETOMETER_ADDRS[SENSOR_COUNT] = {
    MAGNETOMETER_I2C_ADDR,
    MAGNETOMETER_I2C_ADDR,
};

esp_err_t acquisition_capture(acquisition_buffer_t *buffer, uint32_t duration_ms)
{
    if (buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(buffer, 0, sizeof(*buffer));

    size_t requested_samples = duration_ms / ACQ_SAMPLE_MS;
    if (requested_samples == 0) {
        requested_samples = 1;
    }

    if (requested_samples > ACQ_MAX_SAMPLES) {
        requested_samples = ACQ_MAX_SAMPLES;
    }

    for (size_t frame_idx = 0; frame_idx < requested_samples; frame_idx++) {
        acquisition_frame_t *frame = &buffer->frames[frame_idx];

        for (size_t sensor_idx = 0; sensor_idx < SENSOR_COUNT; sensor_idx++) {
            frame->sensors[sensor_idx].channel = MAGNETOMETER_CHANNELS[sensor_idx];
            frame->sensors[sensor_idx].address = MAGNETOMETER_ADDRS[sensor_idx];
            esp_err_t read_result = magnetometer_read_axes(
                frame->sensors[sensor_idx].channel,
                frame->sensors[sensor_idx].address,
                &frame->sensors[sensor_idx].axes
            );

            if (read_result == ESP_OK) {
                const magnetometer_axes_t *axes = &frame->sensors[sensor_idx].axes;

                ESP_LOGI(TAG,
                         "muestra=%u sensor=%u canal=%u raw[x=%d y=%d z=%d] uT[x=%.2f y=%.2f z=%.2f]",
                         (unsigned)frame_idx,
                         (unsigned)sensor_idx,
                         (unsigned)frame->sensors[sensor_idx].channel,
                         axes->x,
                         axes->y,
                         axes->z,
                         magnetometer_raw_to_ut(axes->x),
                         magnetometer_raw_to_ut(axes->y),
                         magnetometer_raw_to_ut(axes->z));
            } else {
                ESP_LOGE(TAG,
                         "Fallo lectura magnetometro sensor=%u canal=%u addr=0x%02x: %s",
                         (unsigned)sensor_idx,
                         (unsigned)frame->sensors[sensor_idx].channel,
                         frame->sensors[sensor_idx].address,
                         esp_err_to_name(read_result));
            }
        }

        buffer->frame_count++;
        vTaskDelay(pdMS_TO_TICKS(ACQ_SAMPLE_MS));
    }

    return ESP_OK;
}
