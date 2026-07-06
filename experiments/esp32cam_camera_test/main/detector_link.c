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

    if (camera_service_capture(crossing_id) == ESP_OK) {
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
    const pending_photo_t *photo = camera_service_pending_photo();
    if (!photo->valid || photo->crossing_id != crossing_id) {
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
             (unsigned)photo->jpeg_size);

    // Fase siguiente: encolar photo->jpeg para Telegram. La foto se conserva
    // en PSRAM hasta recibir un nuevo START o hasta que el futuro emisor la libere.
    send_response("ALERT_PHOTO_READY");
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
