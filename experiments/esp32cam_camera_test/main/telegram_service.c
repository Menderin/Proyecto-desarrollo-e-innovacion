#include "telegram_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "wifi_service.h"

#ifndef CONFIG_CAMERA_TELEGRAM_BOT_TOKEN
#define CONFIG_CAMERA_TELEGRAM_BOT_TOKEN ""
#endif
#ifndef CONFIG_CAMERA_TELEGRAM_CHAT_ID
#define CONFIG_CAMERA_TELEGRAM_CHAT_ID ""
#endif
#ifndef CONFIG_CAMERA_TELEGRAM_RETRIES
#define CONFIG_CAMERA_TELEGRAM_RETRIES 1
#endif
#ifndef CONFIG_CAMERA_TELEGRAM_QUEUE_LENGTH
#define CONFIG_CAMERA_TELEGRAM_QUEUE_LENGTH 1
#endif

static const char *TAG = "TELEGRAM";
static const char *MULTIPART_BOUNDARY = "----ESP32CAMBoundary7MA4YWxk";
static QueueHandle_t s_alert_queue;

static void build_alert_caption(char *caption, size_t caption_size)
{
    time_t now = 0;
    struct tm local_time = {0};
    char date_text[16] = "sin fecha";
    char time_text[16] = "sin hora";

    time(&now);
    if (now > 0) {
        // Chile continental usa UTC-4 en invierno. Es suficiente para la
        // fecha/hora de evaluacion y evita depender de base de zonas horarias.
        setenv("TZ", "<-04>4", 1);
        tzset();
        localtime_r(&now, &local_time);
        strftime(date_text, sizeof(date_text), "%d-%m-%Y", &local_time);
        strftime(time_text, sizeof(time_text), "%H:%M:%S", &local_time);
    }

    snprintf(
        caption,
        caption_size,
        "Posible entrada de objeto punzante.\n"
        "Fecha: %s\n"
        "Hora: %s",
        date_text,
        time_text
    );
}

static esp_err_t http_write_all(esp_http_client_handle_t client,
                                const uint8_t *data,
                                size_t length)
{
    size_t written = 0;
    while (written < length) {
        const int result = esp_http_client_write(
            client,
            (const char *)data + written,
            (int)(length - written)
        );
        if (result <= 0) {
            return ESP_FAIL;
        }
        written += (size_t)result;
    }
    return ESP_OK;
}

static esp_err_t telegram_send_photo(const telegram_alert_t *alert)
{
    if (strlen(CONFIG_CAMERA_TELEGRAM_BOT_TOKEN) == 0 ||
        strlen(CONFIG_CAMERA_TELEGRAM_CHAT_ID) == 0) {
        ESP_LOGE(TAG, "Token o chat ID vacio");
        return ESP_ERR_INVALID_ARG;
    }

    char url[256];
    const int url_len = snprintf(
        url,
        sizeof(url),
        "https://api.telegram.org/bot%s/sendPhoto",
        CONFIG_CAMERA_TELEGRAM_BOT_TOKEN
    );
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char caption[192];
    build_alert_caption(caption, sizeof(caption));

    char prefix[768];
    const int prefix_len = snprintf(
        prefix,
        sizeof(prefix),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"alerta.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        MULTIPART_BOUNDARY,
        CONFIG_CAMERA_TELEGRAM_CHAT_ID,
        MULTIPART_BOUNDARY,
        caption,
        MULTIPART_BOUNDARY
    );
    if (prefix_len < 0 || prefix_len >= (int)sizeof(prefix)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char suffix[64];
    const int suffix_len = snprintf(
        suffix,
        sizeof(suffix),
        "\r\n--%s--\r\n",
        MULTIPART_BOUNDARY
    );
    if (suffix_len < 0 || suffix_len >= (int)sizeof(suffix)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char content_type[96];
    snprintf(
        content_type,
        sizeof(content_type),
        "multipart/form-data; boundary=%s",
        MULTIPART_BOUNDARY
    );

    const size_t content_length =
        (size_t)prefix_len + alert->photo.jpeg_size + (size_t)suffix_len;
    if (content_length > INT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_err_t result = esp_http_client_open(client, (int)content_length);
    if (result == ESP_OK) {
        result = http_write_all(client, (const uint8_t *)prefix, (size_t)prefix_len);
    }
    if (result == ESP_OK) {
        result = http_write_all(client, alert->photo.jpeg, alert->photo.jpeg_size);
    }
    if (result == ESP_OK) {
        result = http_write_all(client, (const uint8_t *)suffix, (size_t)suffix_len);
    }

    int status_code = 0;
    if (result == ESP_OK) {
        const int64_t response_length = esp_http_client_fetch_headers(client);
        status_code = esp_http_client_get_status_code(client);
        if (response_length < 0 || status_code != 200) {
            ESP_LOGE(TAG,
                     "Telegram HTTP status=%d response_length=%" PRId64,
                     status_code,
                     response_length);
            result = ESP_FAIL;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (result == ESP_OK) {
        ESP_LOGI(TAG,
                 "Foto enviada a Telegram id=%" PRIu32 " bytes=%u",
                 alert->photo.crossing_id,
                 (unsigned)alert->photo.jpeg_size);
    }
    return result;
}

static void telegram_worker(void *arg)
{
    (void)arg;
    telegram_alert_t alert;

    while (true) {
        if (xQueueReceive(s_alert_queue, &alert, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t result = ESP_FAIL;
        for (int attempt = 1; attempt <= CONFIG_CAMERA_TELEGRAM_RETRIES; attempt++) {
            if (!wifi_service_wait_connected(pdMS_TO_TICKS(20000))) {
                ESP_LOGW(TAG, "Sin Wi-Fi para alerta; intento %d", attempt);
            } else if (wifi_service_sync_time(pdMS_TO_TICKS(15000)) != ESP_OK) {
                ESP_LOGW(TAG, "Sin hora valida para TLS; intento %d", attempt);
            } else {
                result = telegram_send_photo(&alert);
                if (result == ESP_OK) {
                    break;
                }
                ESP_LOGW(TAG,
                         "Fallo Telegram intento %d: %s",
                         attempt,
                         esp_err_to_name(result));
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
        }

        if (result != ESP_OK) {
            ESP_LOGE(TAG,
                     "Alerta id=%" PRIu32 " agotó reintentos",
                     alert.photo.crossing_id);
        }
        camera_service_release_photo(&alert.photo);
    }
}

esp_err_t telegram_service_init(void)
{
#if !CONFIG_CAMERA_TELEGRAM_ENABLED
    ESP_LOGW(TAG, "Telegram deshabilitado en menuconfig");
    return ESP_OK;
#else
    s_alert_queue = xQueueCreate(
        CONFIG_CAMERA_TELEGRAM_QUEUE_LENGTH,
        sizeof(telegram_alert_t)
    );
    if (s_alert_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreate(
        telegram_worker,
        "telegram_worker",
        12288,
        NULL,
        4,
        NULL
    );
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
#endif
}

esp_err_t telegram_service_enqueue(telegram_alert_t *alert)
{
#if !CONFIG_CAMERA_TELEGRAM_ENABLED
    (void)alert;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (alert == NULL || !alert->photo.valid || s_alert_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_alert_queue, alert, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    memset(alert, 0, sizeof(*alert));
    return ESP_OK;
#endif
}
