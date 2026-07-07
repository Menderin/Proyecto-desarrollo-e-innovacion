#include "edge_impulse_adapter.h"

#include <cstdlib>
#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "detector_thresholds.h"
#include "magnetometer.h"

static const char *TAG = "CLASSIFIER";

static magnetometer_axes_t s_baseline[SENSOR_COUNT] = {};
static int32_t s_threshold_raw = CLASSIFIER_MIN_THRESHOLD_RAW;
static int32_t s_last_p90_raw = 0;
static bool s_calibrated = false;

static int compare_i16(const void *lhs, const void *rhs)
{
    const int16_t a = *static_cast<const int16_t *>(lhs);
    const int16_t b = *static_cast<const int16_t *>(rhs);
    return (a > b) - (a < b);
}

static int compare_u64(const void *lhs, const void *rhs)
{
    const uint64_t a = *static_cast<const uint64_t *>(lhs);
    const uint64_t b = *static_cast<const uint64_t *>(rhs);
    return (a > b) - (a < b);
}

static int16_t median_i16(int16_t *values, size_t count)
{
    qsort(values, count, sizeof(values[0]), compare_i16);
    if ((count % 2U) != 0U) {
        return values[count / 2U];
    }

    const int32_t sum =
        static_cast<int32_t>(values[(count / 2U) - 1U]) + values[count / 2U];
    return static_cast<int16_t>(sum / 2);
}

static uint64_t magnitude_sq(const magnetometer_axes_t *axes,
                             const magnetometer_axes_t *baseline)
{
    const int32_t dx = static_cast<int32_t>(axes->x) - baseline->x;
    const int32_t dy = static_cast<int32_t>(axes->y) - baseline->y;
    const int32_t dz = static_cast<int32_t>(axes->z) - baseline->z;

    return (static_cast<uint64_t>(static_cast<int64_t>(dx) * dx) +
            static_cast<uint64_t>(static_cast<int64_t>(dy) * dy) +
            static_cast<uint64_t>(static_cast<int64_t>(dz) * dz));
}

static int32_t integer_sqrt_u64(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = UINT64_C(1) << 62;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    return static_cast<int32_t>(result);
}

esp_err_t edge_impulse_adapter_init(void)
{
    static int16_t samples_x[SENSOR_COUNT][CLASSIFIER_BASELINE_SAMPLES];
    static int16_t samples_y[SENSOR_COUNT][CLASSIFIER_BASELINE_SAMPLES];
    static int16_t samples_z[SENSOR_COUNT][CLASSIFIER_BASELINE_SAMPLES];
    static magnetometer_axes_t original_samples[SENSOR_COUNT][CLASSIFIER_BASELINE_SAMPLES];
    size_t successful[SENSOR_COUNT] = {};

    s_calibrated = false;
    ESP_LOGI(TAG,
             "Calibrando ambiente: retire objetos durante %u ms",
             (unsigned)(CLASSIFIER_BASELINE_SAMPLES * CLASSIFIER_BASELINE_SAMPLE_MS));

    for (size_t sample_idx = 0; sample_idx < CLASSIFIER_BASELINE_SAMPLES; sample_idx++) {
        for (size_t sensor_idx = 0; sensor_idx < SENSOR_COUNT; sensor_idx++) {
            magnetometer_axes_t axes = {};
            const esp_err_t result = magnetometer_read_axes(
                static_cast<uint8_t>(sensor_idx),
                MAGNETOMETER_I2C_ADDR,
                &axes
            );
            if (result == ESP_OK) {
                const size_t dst = successful[sensor_idx]++;
                samples_x[sensor_idx][dst] = axes.x;
                samples_y[sensor_idx][dst] = axes.y;
                samples_z[sensor_idx][dst] = axes.z;
                original_samples[sensor_idx][dst] = axes;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CLASSIFIER_BASELINE_SAMPLE_MS));
    }

    int32_t observed_noise_raw = 0;
    for (size_t sensor_idx = 0; sensor_idx < SENSOR_COUNT; sensor_idx++) {
        if (successful[sensor_idx] == 0) {
            ESP_LOGE(TAG, "Baseline fallida: sensor %u sin lecturas",
                     (unsigned)sensor_idx);
            return ESP_FAIL;
        }

        s_baseline[sensor_idx].x =
            median_i16(samples_x[sensor_idx], successful[sensor_idx]);
        s_baseline[sensor_idx].y =
            median_i16(samples_y[sensor_idx], successful[sensor_idx]);
        s_baseline[sensor_idx].z =
            median_i16(samples_z[sensor_idx], successful[sensor_idx]);

        uint64_t noise_sq[CLASSIFIER_BASELINE_SAMPLES] = {};
        for (size_t idx = 0; idx < successful[sensor_idx]; idx++) {
            noise_sq[idx] =
                magnitude_sq(&original_samples[sensor_idx][idx],
                             &s_baseline[sensor_idx]);
        }
        qsort(noise_sq, successful[sensor_idx], sizeof(noise_sq[0]), compare_u64);
        const size_t p90_idx = ((successful[sensor_idx] * 9U) + 9U) / 10U - 1U;
        const int32_t sensor_noise_raw = integer_sqrt_u64(noise_sq[p90_idx]);
        if (sensor_noise_raw > observed_noise_raw) {
            observed_noise_raw = sensor_noise_raw;
        }

        ESP_LOGI(TAG,
                 "Baseline sensor=%u muestras=%u raw[x=%d y=%d z=%d] ruido_p90=%ld",
                 (unsigned)sensor_idx,
                 (unsigned)successful[sensor_idx],
                 s_baseline[sensor_idx].x,
                 s_baseline[sensor_idx].y,
                 s_baseline[sensor_idx].z,
                 (long)sensor_noise_raw);
    }

    const int32_t adaptive_threshold =
        (observed_noise_raw * CLASSIFIER_NOISE_MULTIPLIER) +
        CLASSIFIER_NOISE_MARGIN_RAW;
    s_threshold_raw = DETECTOR_MAG_THRESHOLD_RAW;
#ifndef DETECTOR_FEATURE_P90_RAW
    if (s_threshold_raw < CLASSIFIER_MIN_THRESHOLD_RAW) {
        s_threshold_raw = CLASSIFIER_MIN_THRESHOLD_RAW;
    }
#endif
    if (s_threshold_raw < adaptive_threshold) {
        s_threshold_raw = adaptive_threshold;
    }

    s_calibrated = true;
    ESP_LOGI(TAG,
             "Calibracion lista: threshold=%ld (entrenado=%d, ruido=%ld)",
             (long)s_threshold_raw,
             DETECTOR_MAG_THRESHOLD_RAW,
             (long)observed_noise_raw);
    ESP_LOGI(TAG,
             "BASELINE_JSON:{\"samples\":%u,\"noise_p90_raw\":%ld,"
             "\"threshold_raw\":%ld,\"baseline\":{\"x\":%d,\"y\":%d,\"z\":%d}}",
             (unsigned)successful[0],
             (long)observed_noise_raw,
             (long)s_threshold_raw,
             s_baseline[0].x,
             s_baseline[0].y,
             s_baseline[0].z);
    return ESP_OK;
}

classifier_result_t edge_impulse_adapter_run(const acquisition_buffer_t *buffer)
{
    if (!s_calibrated || buffer == nullptr || buffer->frame_count == 0) {
        return CLASSIFIER_RESULT_EMPTY;
    }

    static uint64_t magnitudes_sq[ACQ_MAX_SAMPLES * SENSOR_COUNT];
    size_t magnitude_count = 0;

    for (size_t frame_idx = 0; frame_idx < buffer->frame_count; frame_idx++) {
        const acquisition_frame_t *frame = &buffer->frames[frame_idx];

        for (size_t sensor_idx = 0; sensor_idx < SENSOR_COUNT; sensor_idx++) {
            const magnetometer_axes_t *axes = &frame->sensors[sensor_idx].axes;
            magnitudes_sq[magnitude_count++] =
                magnitude_sq(axes, &s_baseline[sensor_idx]);
        }
    }

    qsort(magnitudes_sq, magnitude_count, sizeof(magnitudes_sq[0]), compare_u64);
    const size_t p90_idx = ((magnitude_count * 9U) + 9U) / 10U - 1U;
    const uint64_t p90_sq = magnitudes_sq[p90_idx];
    const uint64_t threshold_sq =
        static_cast<uint64_t>(s_threshold_raw) * s_threshold_raw;
    s_last_p90_raw = integer_sqrt_u64(p90_sq);
    const classifier_result_t result =
        p90_sq >= threshold_sq ? CLASSIFIER_RESULT_ALERT : CLASSIFIER_RESULT_SAFE;

    ESP_LOGI(TAG,
             "Clasificacion p90=%ld threshold=%ld muestras=%u resultado=%s",
             (long)s_last_p90_raw,
             (long)s_threshold_raw,
             (unsigned)magnitude_count,
             result == CLASSIFIER_RESULT_ALERT ? "alerta" : "seguro");
    ESP_LOGI(TAG,
             "CROSS_JSON:{\"p90_raw\":%ld,\"threshold_raw\":%ld,"
             "\"samples\":%u,\"prediction\":\"%s\"}",
             (long)s_last_p90_raw,
             (long)s_threshold_raw,
             (unsigned)magnitude_count,
             result == CLASSIFIER_RESULT_ALERT ? "dangerous" : "allowed");
    return result;
}

int32_t edge_impulse_adapter_last_p90_raw(void)
{
    return s_last_p90_raw;
}

int32_t edge_impulse_adapter_last_threshold_raw(void)
{
    return s_threshold_raw;
}
