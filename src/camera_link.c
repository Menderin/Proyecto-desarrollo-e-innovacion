#include "camera_link.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CAM_LINK";

static bool s_camera_link_ready = false;

static void camera_link_rx_task(void *arg)
{
    (void)arg;

    uint8_t input[64];
    char line[128];
    size_t line_length = 0;

    while (true) {
        const int read_count = uart_read_bytes(
            CAMERA_LINK_UART,
            input,
            sizeof(input),
            pdMS_TO_TICKS(200)
        );

        for (int idx = 0; idx < read_count; idx++) {
            const char value = (char)input[idx];
            if (value == '\r') {
                continue;
            }
            if (value == '\n') {
                line[line_length] = '\0';
                if (line_length > 0) {
                    ESP_LOGI(TAG, "RX <- ESP32-CAM: %s", line);
                }
                line_length = 0;
                continue;
            }
            if (line_length < sizeof(line) - 1) {
                line[line_length++] = value;
            } else {
                line_length = 0;
                ESP_LOGW(TAG, "Respuesta ESP32-CAM demasiado larga; descartada");
            }
        }
    }
}

static void camera_link_send_line(const char *line)
{
#if APP_CAMERA_LINK_ENABLED
    if (!s_camera_link_ready || line == NULL) {
        return;
    }

    uart_write_bytes(CAMERA_LINK_UART, line, strlen(line));
    uart_write_bytes(CAMERA_LINK_UART, "\n", 1);
    ESP_LOGI(TAG, "TX -> ESP32-CAM: %s", line);
#else
    (void)line;
#endif
}

esp_err_t camera_link_init(void)
{
#if APP_CAMERA_LINK_ENABLED
    const uart_config_t config = {
        .baud_rate = CAMERA_LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install(
        CAMERA_LINK_UART,
        CAMERA_LINK_RX_BUF_SIZE,
        0,
        0,
        NULL,
        0
    );
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "No se pudo instalar UART camara: %s", esp_err_to_name(result));
        return result;
    }

    result = uart_param_config(CAMERA_LINK_UART, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar UART camara: %s", esp_err_to_name(result));
        return result;
    }

    result = uart_set_pin(
        CAMERA_LINK_UART,
        CAMERA_LINK_TX_PIN,
        CAMERA_LINK_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudieron asignar pines UART camara: %s", esp_err_to_name(result));
        return result;
    }

    s_camera_link_ready = true;
    ESP_LOGI(TAG,
             "Enlace ESP32-CAM listo: TX=GPIO%d RX=GPIO%d baud=%d",
             CAMERA_LINK_TX_PIN,
             CAMERA_LINK_RX_PIN,
             CAMERA_LINK_BAUD);
    const BaseType_t created = xTaskCreate(
        camera_link_rx_task,
        "camera_link_rx",
        3072,
        NULL,
        4,
        NULL
    );
    if (created != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear tarea RX ESP32-CAM");
        return ESP_ERR_NO_MEM;
    }
    camera_link_send_ping();
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Enlace ESP32-CAM deshabilitado");
    return ESP_OK;
#endif
}

void camera_link_send_ping(void)
{
    camera_link_send_line("PING");
}

void camera_link_send_start(uint32_t crossing_id, const char *direction)
{
    char line[64];
    snprintf(line, sizeof(line), "START,%lu,%s", (unsigned long)crossing_id, direction);
    camera_link_send_line(line);
}

void camera_link_send_safe(uint32_t crossing_id)
{
    char line[32];
    snprintf(line, sizeof(line), "SAFE,%lu", (unsigned long)crossing_id);
    camera_link_send_line(line);
}

void camera_link_send_alert(uint32_t crossing_id, int32_t p90_raw, int32_t threshold_raw)
{
    char line[64];
    snprintf(line,
             sizeof(line),
             "ALERT,%lu,%ld,%ld",
             (unsigned long)crossing_id,
             (long)p90_raw,
             (long)threshold_raw);
    camera_link_send_line(line);
}
