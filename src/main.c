#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "acquisition.h"
#include "classifier.h"
#include "detector_thresholds.h"
#include "i2c_bus.h"
#include "ir_sensor.h"
#include "magnetometer.h"
#include "main.h"
#include "tca9548a.h"

static const char *TAG = "APP";

typedef enum {
    CROSSING_START_NONE = 0,
    CROSSING_START_IR1,
    CROSSING_START_IR2,
} crossing_start_t;

#if APP_CHARACTERIZATION_MODE || APP_DETECTOR_MODE
typedef struct {
    magnetometer_axes_t median;
    magnetometer_axes_t min;
    magnetometer_axes_t max;
    size_t samples;
} magnetic_window_t;

typedef struct {
    magnetometer_axes_t axes;
    int32_t threshold_raw;
    int32_t noise_raw;
} detector_baseline_t;

static int64_t magnetic_delta_magnitude_sq(const magnetometer_axes_t *value,
                                           const magnetometer_axes_t *baseline)
{
    int32_t dx = (int32_t)value->x - baseline->x;
    int32_t dy = (int32_t)value->y - baseline->y;
    int32_t dz = (int32_t)value->z - baseline->z;

    return ((int64_t)dx * dx) + ((int64_t)dy * dy) + ((int64_t)dz * dz);
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t max_i32(int32_t lhs, int32_t rhs)
{
    return lhs > rhs ? lhs : rhs;
}

static int32_t magnetic_axis_distance_raw(const magnetometer_axes_t *value,
                                          const magnetometer_axes_t *baseline)
{
    int32_t dx = abs_i32((int32_t)value->x - baseline->x);
    int32_t dy = abs_i32((int32_t)value->y - baseline->y);
    int32_t dz = abs_i32((int32_t)value->z - baseline->z);

    return max_i32(dx, max_i32(dy, dz));
}

#define CHAR_MAX_SAMPLES (CHAR_WINDOW_MS / CHAR_SAMPLE_MS)
#define DETECTOR_MAX_SAMPLES (DETECTOR_WINDOW_MS / DETECTOR_SAMPLE_MS)

static int compare_i16(const void *a, const void *b)
{
    int16_t lhs = *(const int16_t *)a;
    int16_t rhs = *(const int16_t *)b;

    return (lhs > rhs) - (lhs < rhs);
}

static int16_t median_i16(int16_t *values, size_t count)
{
    if (count == 0) {
        return 0;
    }

    qsort(values, count, sizeof(values[0]), compare_i16);

    if ((count % 2) == 0) {
        int32_t sum = (int32_t)values[(count / 2) - 1] + values[count / 2];
        return (int16_t)(sum / 2);
    }

    return values[count / 2];
}
#endif

#if APP_CHARACTERIZATION_MODE

static void run_characterization_mode(void)
{
    static int16_t samples_x[CHAR_MAX_SAMPLES];
    static int16_t samples_y[CHAR_MAX_SAMPLES];
    static int16_t samples_z[CHAR_MAX_SAMPLES];
    uint32_t sequence = 0;

    ESP_LOGI(TAG, "Modo caracterizacion activo: ventana=%u ms sample=%u ms",
             (unsigned)CHAR_WINDOW_MS, (unsigned)CHAR_SAMPLE_MS);

    while (true) {
        size_t successful_reads = 0;
        int16_t min_x = INT16_MAX;
        int16_t min_y = INT16_MAX;
        int16_t min_z = INT16_MAX;
        int16_t max_x = INT16_MIN;
        int16_t max_y = INT16_MIN;
        int16_t max_z = INT16_MIN;

        for (size_t idx = 0; idx < CHAR_MAX_SAMPLES; idx++) {
            magnetometer_axes_t axes = {0};
            esp_err_t result = magnetometer_read_axes(0, MAGNETOMETER_I2C_ADDR, &axes);
            if (result == ESP_OK) {
                samples_x[successful_reads] = axes.x;
                samples_y[successful_reads] = axes.y;
                samples_z[successful_reads] = axes.z;

                if (axes.x < min_x) {
                    min_x = axes.x;
                }
                if (axes.y < min_y) {
                    min_y = axes.y;
                }
                if (axes.z < min_z) {
                    min_z = axes.z;
                }
                if (axes.x > max_x) {
                    max_x = axes.x;
                }
                if (axes.y > max_y) {
                    max_y = axes.y;
                }
                if (axes.z > max_z) {
                    max_z = axes.z;
                }

                successful_reads++;
            } else {
                ESP_LOGE(TAG, "Caracterizacion: fallo lectura magnetometro: %s",
                         esp_err_to_name(result));
            }

            vTaskDelay(pdMS_TO_TICKS(CHAR_SAMPLE_MS));
        }

        if (successful_reads == 0) {
            ESP_LOGE(TAG, "CHAR_JSON:{\"seq\":%u,\"ok\":false,\"samples\":0}",
                     (unsigned)sequence);
            sequence++;
            continue;
        }

        int16_t med_x = median_i16(samples_x, successful_reads);
        int16_t med_y = median_i16(samples_y, successful_reads);
        int16_t med_z = median_i16(samples_z, successful_reads);

        ESP_LOGI(TAG,
                 "CHAR_JSON:{\"seq\":%u,\"ok\":true,\"window_ms\":%u,\"sample_ms\":%u,"
                 "\"samples\":%u,\"med_raw\":{\"x\":%d,\"y\":%d,\"z\":%d},"
                 "\"min_raw\":{\"x\":%d,\"y\":%d,\"z\":%d},"
                 "\"max_raw\":{\"x\":%d,\"y\":%d,\"z\":%d}}",
                 (unsigned)sequence,
                 (unsigned)CHAR_WINDOW_MS,
                 (unsigned)CHAR_SAMPLE_MS,
                 (unsigned)successful_reads,
                 med_x,
                 med_y,
                 med_z,
                 min_x,
                 min_y,
                 min_z,
                 max_x,
                 max_y,
                 max_z);

        sequence++;
    }
}
#endif

#if APP_DETECTOR_MODE
static esp_err_t capture_detector_window(magnetic_window_t *window)
{
    if (window == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static int16_t samples_x[DETECTOR_MAX_SAMPLES];
    static int16_t samples_y[DETECTOR_MAX_SAMPLES];
    static int16_t samples_z[DETECTOR_MAX_SAMPLES];
    size_t successful_reads = 0;

    window->min.x = INT16_MAX;
    window->min.y = INT16_MAX;
    window->min.z = INT16_MAX;
    window->max.x = INT16_MIN;
    window->max.y = INT16_MIN;
    window->max.z = INT16_MIN;
    window->samples = 0;

    for (size_t idx = 0; idx < DETECTOR_MAX_SAMPLES; idx++) {
        magnetometer_axes_t axes = {0};
        esp_err_t result = magnetometer_read_axes(0, MAGNETOMETER_I2C_ADDR, &axes);
        if (result == ESP_OK) {
            samples_x[successful_reads] = axes.x;
            samples_y[successful_reads] = axes.y;
            samples_z[successful_reads] = axes.z;

            if (axes.x < window->min.x) {
                window->min.x = axes.x;
            }
            if (axes.y < window->min.y) {
                window->min.y = axes.y;
            }
            if (axes.z < window->min.z) {
                window->min.z = axes.z;
            }
            if (axes.x > window->max.x) {
                window->max.x = axes.x;
            }
            if (axes.y > window->max.y) {
                window->max.y = axes.y;
            }
            if (axes.z > window->max.z) {
                window->max.z = axes.z;
            }

            successful_reads++;
        } else {
            ESP_LOGE(TAG, "Detector: fallo lectura magnetometro: %s",
                     esp_err_to_name(result));
        }

        vTaskDelay(pdMS_TO_TICKS(DETECTOR_SAMPLE_MS));
    }

    if (successful_reads == 0) {
        return ESP_FAIL;
    }

    window->median.x = median_i16(samples_x, successful_reads);
    window->median.y = median_i16(samples_y, successful_reads);
    window->median.z = median_i16(samples_z, successful_reads);
    window->samples = successful_reads;

    return ESP_OK;
}

static esp_err_t calibrate_detector_baseline(detector_baseline_t *baseline)
{
    if (baseline == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static int16_t baseline_x[DETECTOR_BASELINE_WINDOWS];
    static int16_t baseline_y[DETECTOR_BASELINE_WINDOWS];
    static int16_t baseline_z[DETECTOR_BASELINE_WINDOWS];
    magnetic_window_t windows[DETECTOR_BASELINE_WINDOWS];
    size_t successful_windows = 0;

    ESP_LOGI(TAG, "Calibrando baseline: %u ventanas de %u ms sin objetos cerca",
             (unsigned)DETECTOR_BASELINE_WINDOWS,
             (unsigned)DETECTOR_WINDOW_MS);

    for (size_t idx = 0; idx < DETECTOR_BASELINE_WINDOWS; idx++) {
        magnetic_window_t window = {0};
        esp_err_t result = capture_detector_window(&window);
        if (result == ESP_OK) {
            windows[successful_windows] = window;
            baseline_x[successful_windows] = window.median.x;
            baseline_y[successful_windows] = window.median.y;
            baseline_z[successful_windows] = window.median.z;
            successful_windows++;
            ESP_LOGI(TAG, "Baseline ventana %u/%u med[x=%d y=%d z=%d]",
                     (unsigned)successful_windows,
                     (unsigned)DETECTOR_BASELINE_WINDOWS,
                     window.median.x,
                     window.median.y,
                     window.median.z);
        } else {
            ESP_LOGE(TAG, "Fallo calibrando baseline: %s", esp_err_to_name(result));
        }
    }

    if (successful_windows == 0) {
        return ESP_FAIL;
    }

    baseline->axes.x = median_i16(baseline_x, successful_windows);
    baseline->axes.y = median_i16(baseline_y, successful_windows);
    baseline->axes.z = median_i16(baseline_z, successful_windows);

    int32_t noise_raw = 0;
    for (size_t idx = 0; idx < successful_windows; idx++) {
        int32_t distance = magnetic_axis_distance_raw(&windows[idx].median, &baseline->axes);
        if (distance > noise_raw) {
            noise_raw = distance;
        }
    }

    int32_t local_threshold =
        (noise_raw * DETECTOR_NOISE_MULTIPLIER) + DETECTOR_NOISE_MARGIN_RAW;
    baseline->noise_raw = noise_raw;
    baseline->threshold_raw = max_i32(DETECTOR_MAG_THRESHOLD_RAW,
                                      max_i32(DETECTOR_MIN_THRESHOLD_RAW, local_threshold));

    ESP_LOGI(TAG,
             "Baseline detector listo raw[x=%d y=%d z=%d] noise=%ld threshold=%ld",
             baseline->axes.x,
             baseline->axes.y,
             baseline->axes.z,
             (long)baseline->noise_raw,
             (long)baseline->threshold_raw);
    return ESP_OK;
}

static void run_detector_mode(void)
{
    detector_baseline_t baseline = {0};
    uint32_t sequence = 0;
    uint8_t consecutive_hits = 0;
    uint8_t stable_alert_windows = 0;
    magnetometer_axes_t stable_alert_reference = {0};

    ESP_LOGI(TAG,
             "Modo detector activo: threshold_entrenado=%d raw min=%d ventanas=%u ms",
             DETECTOR_MAG_THRESHOLD_RAW,
             DETECTOR_MIN_THRESHOLD_RAW,
             (unsigned)DETECTOR_WINDOW_MS);

    esp_err_t baseline_result = calibrate_detector_baseline(&baseline);
    if (baseline_result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo calibrar baseline detector: %s",
                 esp_err_to_name(baseline_result));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    while (true) {
        magnetic_window_t window = {0};
        esp_err_t result = capture_detector_window(&window);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Detector: ventana invalida: %s", esp_err_to_name(result));
            continue;
        }

        int32_t dx = (int32_t)window.median.x - baseline.axes.x;
        int32_t dy = (int32_t)window.median.y - baseline.axes.y;
        int32_t dz = (int32_t)window.median.z - baseline.axes.z;
        int64_t mag_sq = magnetic_delta_magnitude_sq(&window.median, &baseline.axes);
        int64_t threshold_sq = (int64_t)baseline.threshold_raw * baseline.threshold_raw;
        bool hit = mag_sq >= threshold_sq;

        if (hit && consecutive_hits < UINT8_MAX) {
            consecutive_hits++;
        } else if (!hit) {
            consecutive_hits = 0;
        }

        bool alert = consecutive_hits >= DETECTOR_CONSECUTIVE_HITS;

        if (alert) {
            if (stable_alert_windows == 0) {
                stable_alert_reference = window.median;
                stable_alert_windows = 1;
            } else if (magnetic_axis_distance_raw(&window.median, &stable_alert_reference) <=
                       DETECTOR_AUTO_REBASELINE_STABILITY_RAW) {
                if (stable_alert_windows < UINT8_MAX) {
                    stable_alert_windows++;
                }
            } else {
                stable_alert_reference = window.median;
                stable_alert_windows = 1;
            }
        } else {
            stable_alert_windows = 0;
        }

        ESP_LOGI(TAG,
                 "DETECT seq=%u estado=%s med[x=%d y=%d z=%d] "
                 "delta[x=%ld y=%ld z=%ld] mag2=%lld threshold2=%lld hits=%u/%u stable_alert=%u/%u",
                 (unsigned)sequence,
                 alert ? "ALERTA" : "OK",
                 window.median.x,
                 window.median.y,
                 window.median.z,
                 (long)dx,
                 (long)dy,
                 (long)dz,
                 (long long)mag_sq,
                 (long long)threshold_sq,
                 (unsigned)consecutive_hits,
                 (unsigned)DETECTOR_CONSECUTIVE_HITS,
                 (unsigned)stable_alert_windows,
                 (unsigned)DETECTOR_AUTO_REBASELINE_WINDOWS);

        if (stable_alert_windows >= DETECTOR_AUTO_REBASELINE_WINDOWS) {
            ESP_LOGW(TAG,
                     "Alerta estable prolongada: recalibrando baseline de ambiente");
            if (calibrate_detector_baseline(&baseline) == ESP_OK) {
                consecutive_hits = 0;
                stable_alert_windows = 0;
            } else {
                ESP_LOGE(TAG, "Recalibracion fallida; se mantiene baseline anterior");
                stable_alert_windows = 0;
            }
        }

        sequence++;
    }
}
#endif

#if APP_DIAG_MUX_ONLY
static void scan_i2c_main_bus(void)
{
    uint8_t found_count = 0;

    ESP_LOGI(TAG, "--- Escaneo I2C bus principal ---");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t result = i2c_bus_probe(addr, 20);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Dispositivo I2C detectado en 0x%02X", addr);
            found_count++;
        }
    }

    if (found_count == 0) {
        ESP_LOGW(TAG, "No se detectaron dispositivos en el bus I2C principal");
    } else {
        ESP_LOGI(TAG, "Escaneo I2C bus principal completo: %u dispositivo(s)", found_count);
    }
}
#endif

#if !APP_CHARACTERIZATION_MODE && !APP_DETECTOR_MODE && !APP_DIAG_MUX_ONLY
static crossing_start_t detect_crossing_start(ir_state_t previous, ir_state_t current)
{
    bool ir1_edge = current.ir1_detected && !previous.ir1_detected;
    bool ir2_edge = current.ir2_detected && !previous.ir2_detected;

    if (ir1_edge && !ir2_edge) {
        return CROSSING_START_IR1;
    }
    if (ir2_edge && !ir1_edge) {
        return CROSSING_START_IR2;
    }
    if (ir1_edge && ir2_edge) {
        return CROSSING_START_IR1;
    }

    return CROSSING_START_NONE;
}

static bool crossing_end_detected(crossing_start_t start, ir_state_t state)
{
    if (start == CROSSING_START_IR1) {
        return state.ir2_detected;
    }
    if (start == CROSSING_START_IR2) {
        return state.ir1_detected;
    }

    return false;
}

static const char *crossing_start_to_string(crossing_start_t start)
{
    switch (start) {
    case CROSSING_START_IR1:
        return "IR1->IR2";
    case CROSSING_START_IR2:
        return "IR2->IR1";
    default:
        return "NONE";
    }
}

static void wait_crossing_clear(void)
{
    uint32_t clear_ms = 0;

    while (clear_ms < CROSSING_CLEAR_MS) {
        ir_state_t state = ir_sensor_read();
        if (ir_sensor_any_detected(state)) {
            clear_ms = 0;
        } else {
            clear_ms += CROSSING_POLL_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(CROSSING_POLL_MS));
    }
}

static esp_err_t capture_crossing(acquisition_buffer_t *buffer,
                                  crossing_start_t start,
                                  bool *ended_by_opposite_sensor)
{
    if (buffer == NULL || ended_by_opposite_sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *ended_by_opposite_sensor = false;
    memset(buffer, 0, sizeof(*buffer));

    uint32_t elapsed_ms = 0;
    size_t successful_reads = 0;

    while (elapsed_ms < CROSSING_TIMEOUT_MS && buffer->frame_count < ACQ_MAX_SAMPLES) {
        acquisition_frame_t *frame = &buffer->frames[buffer->frame_count];

        for (size_t sensor_idx = 0; sensor_idx < SENSOR_COUNT; sensor_idx++) {
            frame->sensors[sensor_idx].channel = 0;
            frame->sensors[sensor_idx].address = MAGNETOMETER_I2C_ADDR;
            esp_err_t read_result = magnetometer_read_axes(
                frame->sensors[sensor_idx].channel,
                frame->sensors[sensor_idx].address,
                &frame->sensors[sensor_idx].axes
            );

            if (read_result == ESP_OK) {
                successful_reads++;
            } else {
                ESP_LOGE(TAG,
                         "Cruce: fallo lectura magnetometro sensor=%u: %s",
                         (unsigned)sensor_idx,
                         esp_err_to_name(read_result));
            }
        }

        buffer->frame_count++;

        ir_state_t state = ir_sensor_read();
        if (crossing_end_detected(start, state)) {
            *ended_by_opposite_sensor = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(ACQ_SAMPLE_MS));
        elapsed_ms += ACQ_SAMPLE_MS;
    }

    if (successful_reads == 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando portico de clasificacion magnetica");

    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_LOGI(TAG, "I2C inicializado correctamente");
    ESP_LOGI(TAG, "I2C config: SDA=GPIO%d SCL=GPIO%d freq=%d Hz",
             I2C_MASTER_SDA, I2C_MASTER_SCL, I2C_FREQ_HZ);

#if APP_DIAG_MUX_ONLY
    scan_i2c_main_bus();
#endif

#if APP_USE_I2C_MUX
    esp_err_t probe_res = i2c_bus_probe(TCA9548A_ADDR, 100);
    if (probe_res == ESP_OK) {
        ESP_LOGI(TAG, "TCA9548A encontrado en 0x%02X - bus I2C OK", TCA9548A_ADDR);
    } else {
        ESP_LOGE(TAG, "TCA9548A NO detectado (0x%02X) err=%s - verificar SDA/SCL y pull-ups",
                 TCA9548A_ADDR, esp_err_to_name(probe_res));
    }
#else
    esp_err_t probe_res = i2c_bus_probe(MAGNETOMETER_I2C_ADDR, 100);
    if (probe_res == ESP_OK) {
        ESP_LOGI(TAG, "Magnetometro directo encontrado en 0x%02X - bus I2C OK",
                 MAGNETOMETER_I2C_ADDR);
    } else {
        ESP_LOGE(TAG, "Magnetometro directo NO detectado (0x%02X) err=%s",
                 MAGNETOMETER_I2C_ADDR, esp_err_to_name(probe_res));
    }
#endif

#if APP_DIAG_MUX_ONLY
    ESP_LOGI(TAG, "Modo diagnostico I2C activo");

#if APP_USE_I2C_MUX
    if (probe_res == ESP_OK) {
        for (uint8_t channel = 0; channel < 8; channel++) {
            esp_err_t select_res = tca9548a_select_channel(channel);
            if (select_res == ESP_OK) {
                ESP_LOGI(TAG, "TCA9548A canal %u seleccionado OK", channel);
            } else {
                ESP_LOGE(TAG, "TCA9548A fallo al seleccionar canal %u: %s",
                         channel, esp_err_to_name(select_res));
            }
        }

        esp_err_t disable_res = tca9548a_disable_all();
        if (disable_res == ESP_OK) {
            ESP_LOGI(TAG, "TCA9548A canales deshabilitados OK");
        } else {
            ESP_LOGE(TAG, "TCA9548A fallo al deshabilitar canales: %s",
                     esp_err_to_name(disable_res));
        }
    }
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif

#if APP_CHARACTERIZATION_MODE
    esp_err_t char_mag_result = magnetometer_init(0, MAGNETOMETER_I2C_ADDR);
    if (char_mag_result == ESP_OK) {
        ESP_LOGI(TAG, "Magnetometro listo para caracterizacion");
        run_characterization_mode();
    }

    ESP_LOGE(TAG, "No se pudo iniciar modo caracterizacion: %s",
             esp_err_to_name(char_mag_result));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif

#if APP_DETECTOR_MODE
    esp_err_t detector_mag_result = magnetometer_init(0, MAGNETOMETER_I2C_ADDR);
    if (detector_mag_result == ESP_OK) {
        ESP_LOGI(TAG, "Magnetometro listo para detector");
        run_detector_mode();
    }

    ESP_LOGE(TAG, "No se pudo iniciar modo detector: %s",
             esp_err_to_name(detector_mag_result));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif

#if !APP_CHARACTERIZATION_MODE && !APP_DETECTOR_MODE && !APP_DIAG_MUX_ONLY
    ESP_ERROR_CHECK(ir_sensor_init());
    ESP_LOGI(TAG, "GPIOs IR inicializados correctamente");

#if APP_USE_I2C_MUX
    ESP_LOGI(TAG, "--- Escaneo I2C de diagnostico ---");
    tca9548a_scan_channels(0, SENSOR_COUNT);
    ESP_LOGI(TAG, "--- Fin escaneo ---");
#endif

    for (uint8_t sensor_idx = 0; sensor_idx < SENSOR_COUNT; sensor_idx++) {
        esp_err_t mag_result = magnetometer_init(sensor_idx, MAGNETOMETER_I2C_ADDR);
        if (mag_result == ESP_OK) {
            ESP_LOGI(TAG, "Magnetometro sensor %u inicializado en 0x%02x",
                     sensor_idx,
                     MAGNETOMETER_I2C_ADDR);
        } else {
            ESP_LOGE(TAG, "No se pudo inicializar magnetometro sensor %u: %s",
                     sensor_idx,
                     esp_err_to_name(mag_result));
        }
    }

    ESP_ERROR_CHECK(classifier_init());
    ESP_LOGI(TAG, "Clasificador inicializado");

    ir_state_t previous_ir_state = ir_sensor_read();
    ESP_LOGI(TAG, "Monitoreo de cruce activo: IR1=GPIO%d IR2=GPIO%d",
             PIN_IR_1, PIN_IR_2);

    while (true) {
        ir_state_t ir_state = ir_sensor_read();
        crossing_start_t crossing_start = detect_crossing_start(previous_ir_state, ir_state);
        previous_ir_state = ir_state;

        if (crossing_start == CROSSING_START_NONE) {
            vTaskDelay(pdMS_TO_TICKS(CROSSING_POLL_MS));
            continue;
        }

        ESP_LOGI(TAG, "Cruce iniciado direccion=%s",
                 crossing_start_to_string(crossing_start));

        static acquisition_buffer_t buffer;
        bool ended_by_opposite_sensor = false;
        esp_err_t acq_result = capture_crossing(
            &buffer,
            crossing_start,
            &ended_by_opposite_sensor
        );

        if (acq_result != ESP_OK) {
            ESP_LOGE(TAG, "Fallo en adquisicion de cruce: %s",
                     esp_err_to_name(acq_result));
        } else {
            ESP_LOGI(TAG,
                     "Cruce terminado direccion=%s frames=%u cierre=%s",
                     crossing_start_to_string(crossing_start),
                     (unsigned)buffer.frame_count,
                     ended_by_opposite_sensor ? "sensor_opuesto" : "timeout_o_buffer_lleno");

            classifier_result_t result = classifier_run(&buffer);
            ESP_LOGI(TAG, "Resultado clasificacion: %s",
                     classifier_result_to_string(result));
        }

        wait_crossing_clear();
        previous_ir_state = ir_sensor_read();
        ESP_LOGI(TAG, "Cruce despejado; listo para nueva deteccion");
    }
#endif
}
