#include "detector_link.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "camera_node_config.h"
#include "camera_service.h"
#include "telegram_service.h"

static const char *TAG = "DETECTOR_LINK";

static void send_response(const char *response)
{
    uart_write_bytes(DETECTOR_UART_PORT, response, strlen(response));
    uart_write_bytes(DETECTOR_UART_PORT, "\n", 1);
}

static void handle_start(char *save_ptr)
{
    const char *id_text = strtok_r(NULL, ",", &save_ptr);
    const char *direction = strtok_r(NULL, ",", &save_ptr);
    if (id_text == NULL) {
        send_response("ERROR,START_FORMAT");
        return;
    }

    const uint32_t crossing_id = (uint32_t)strtoul(id_text, NULL, 10);
    ESP_LOGI(TAG,
             "START id=%" PRIu32 " direction=%s",
             crossing_id,
             direction != NULL ? direction : "UNKNOWN");

    if (camera_service_capture(crossing_id, direction) == ESP_OK) {
        char response[48];
        snprintf(response, sizeof(response), "PHOTO_OK,%" PRIu32, crossing_id);
        send_response(response);
    } else {
        char response[48];
        snprintf(response, sizeof(response), "PHOTO_FAIL,%" PRIu32, crossing_id);
        send_response(response);
    }
}

static void handle_safe(char *save_ptr)
{
    const char *id_text = strtok_r(NULL, ",", &save_ptr);
    if (id_text == NULL) {
        send_response("ERROR,SAFE_FORMAT");
        return;
    }

    const uint32_t crossing_id = (uint32_t)strtoul(id_text, NULL, 10);
    camera_service_discard(crossing_id);
    send_response("SAFE_OK");
}

static void handle_alert(char *save_ptr)
{
    const char *id_text = strtok_r(NULL, ",", &save_ptr);
    const char *p90_text = strtok_r(NULL, ",", &save_ptr);
    const char *threshold_text = strtok_r(NULL, ",", &save_ptr);
    if (id_text == NULL || p90_text == NULL || threshold_text == NULL) {
        send_response("ERROR,ALERT_FORMAT");
        return;
    }

    const uint32_t crossing_id = (uint32_t)strtoul(id_text, NULL, 10);
    telegram_alert_t alert = {
        .p90_raw = (int32_t)strtol(p90_text, NULL, 10),
        .threshold_raw = (int32_t)strtol(threshold_text, NULL, 10),
    };
    if (camera_service_take_pending(crossing_id, &alert.photo) != ESP_OK) {
        ESP_LOGE(TAG, "ALERT id=%" PRIu32 " sin foto asociada", crossing_id);
        send_response("ALERT_NO_PHOTO");
        return;
    }

    ESP_LOGW(TAG,
             "PHOTO_READY_FOR_TELEGRAM id=%" PRIu32
             " p90=%s threshold=%s bytes=%u",
             crossing_id,
             p90_text,
             threshold_text,
             (unsigned)alert.photo.jpeg_size);

    const esp_err_t result = telegram_service_enqueue(&alert);
    if (result == ESP_OK) {
        send_response("ALERT_QUEUED");
    } else {
        ESP_LOGE(TAG, "No se pudo encolar Telegram: %s", esp_err_to_name(result));
        camera_service_release_photo(&alert.photo);
        send_response("ALERT_QUEUE_FAIL");
    }
}

static void process_line(char *line)
{
    char *save_ptr = NULL;
    const char *command = strtok_r(line, ",", &save_ptr);
    if (command == NULL) {
        return;
    }

    if (strcmp(command, "START") == 0) {
        handle_start(save_ptr);
    } else if (strcmp(command, "SAFE") == 0) {
        handle_safe(save_ptr);
    } else if (strcmp(command, "ALERT") == 0) {
        handle_alert(save_ptr);
    } else if (strcmp(command, "PING") == 0) {
        send_response("READY");
    } else {
        ESP_LOGW(TAG, "Comando desconocido: %s", command);
        send_response("ERROR,UNKNOWN_COMMAND");
    }
}

esp_err_t detector_link_init(void)
{
    const uart_config_t config = {
        .baud_rate = DETECTOR_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install(
        DETECTOR_UART_PORT,
        DETECTOR_UART_RX_SIZE,
        0,
        0,
        NULL,
        0
    );
    if (result != ESP_OK) {
        return result;
    }
    result = uart_param_config(DETECTOR_UART_PORT, &config);
    if (result != ESP_OK) {
        return result;
    }
    return uart_set_pin(
        DETECTOR_UART_PORT,
        DETECTOR_UART_TX_PIN,
        DETECTOR_UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );
}

void detector_link_run(void)
{
    uint8_t input[64];
    char line[CAMERA_NODE_LINE_MAX];
    size_t line_length = 0;

    send_response("READY");
    ESP_LOGI(TAG,
             "UART lista RX=GPIO%d TX=GPIO%d baud=%d",
             DETECTOR_UART_RX_PIN,
             DETECTOR_UART_TX_PIN,
             DETECTOR_UART_BAUD);

    while (true) {
        const int read_count = uart_read_bytes(
            DETECTOR_UART_PORT,
            input,
            sizeof(input),
            pdMS_TO_TICKS(100)
        );

        for (int idx = 0; idx < read_count; idx++) {
            const char value = (char)input[idx];
            if (value == '\r') {
                continue;
            }
            if (value == '\n') {
                line[line_length] = '\0';
                if (line_length > 0) {
                    process_line(line);
                }
                line_length = 0;
                continue;
            }
            if (line_length < sizeof(line) - 1) {
                line[line_length++] = value;
            } else {
                line_length = 0;
                send_response("ERROR,LINE_TOO_LONG");
            }
        }
    }
}
