#include "edge_impulse_adapter.h"

#include <cstdlib>

esp_err_t edge_impulse_adapter_init(void)
{
    return ESP_OK;
}

classifier_result_t edge_impulse_adapter_run(const acquisition_buffer_t *buffer)
{
    if (buffer == nullptr || buffer->frame_count == 0) {
        return CLASSIFIER_RESULT_EMPTY;
    }

    long total_energy = 0;

    for (size_t frame_idx = 0; frame_idx < buffer->frame_count; frame_idx++) {
        const acquisition_frame_t *frame = &buffer->frames[frame_idx];

        for (size_t sensor_idx = 0; sensor_idx < SENSOR_COUNT; sensor_idx++) {
            const magnetometer_axes_t *axes = &frame->sensors[sensor_idx].axes;
            total_energy += std::abs((int)axes->x);
            total_energy += std::abs((int)axes->y);
            total_energy += std::abs((int)axes->z);
        }
    }

    return total_energy > 0 ? CLASSIFIER_RESULT_UNKNOWN_METAL : CLASSIFIER_RESULT_EMPTY;
}
