#include "camera_service.h"

#include <string.h>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "camera_node_config.h"

static const char *TAG = "CAMERA";
static pending_photo_t s_pending = {0};

static void release_pending_photo(void)
{
    if (s_pending.jpeg != NULL) {
        heap_caps_free(s_pending.jpeg);
    }
    memset(&s_pending, 0, sizeof(s_pending));
}

esp_err_t camera_service_init(void)
{
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM no inicializada; revisa sdkconfig.defaults");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const camera_config_t config = {
        .pin_pwdn = CAMERA_PIN_PWDN,
        .pin_reset = CAMERA_PIN_RESET,
        .pin_xclk = CAMERA_PIN_XCLK,
        .pin_sccb_sda = CAMERA_PIN_SIOD,
        .pin_sccb_scl = CAMERA_PIN_SIOC,
        .pin_d7 = CAMERA_PIN_Y9,
        .pin_d6 = CAMERA_PIN_Y8,
        .pin_d5 = CAMERA_PIN_Y7,
        .pin_d4 = CAMERA_PIN_Y6,
        .pin_d3 = CAMERA_PIN_Y5,
        .pin_d2 = CAMERA_PIN_Y4,
        .pin_d1 = CAMERA_PIN_Y3,
        .pin_d0 = CAMERA_PIN_Y2,
        .pin_vsync = CAMERA_PIN_VSYNC,
        .pin_href = CAMERA_PIN_HREF,
        .pin_pclk = CAMERA_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    const esp_err_t result = esp_camera_init(&config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar OV2640: %s", esp_err_to_name(result));
        return result;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        ESP_LOGI(TAG, "OV2640 lista pid=0x%02x ver=0x%02x", sensor->id.PID, sensor->id.VER);
    }
    return ESP_OK;
}

esp_err_t camera_service_capture(uint32_t crossing_id)
{
    release_pending_photo();

    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == NULL) {
        ESP_LOGE(TAG, "Cruce %lu: no se obtuvo frame", (unsigned long)crossing_id);
        return ESP_FAIL;
    }

    uint8_t *jpeg_copy = heap_caps_malloc(
        frame->len,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (jpeg_copy == NULL) {
        ESP_LOGE(TAG,
                 "Cruce %lu: sin PSRAM para copiar %u bytes",
                 (unsigned long)crossing_id,
                 (unsigned)frame->len);
        esp_camera_fb_return(frame);
        return ESP_ERR_NO_MEM;
    }

    memcpy(jpeg_copy, frame->buf, frame->len);
    s_pending.jpeg = jpeg_copy;
    s_pending.jpeg_size = frame->len;
    s_pending.crossing_id = crossing_id;
    s_pending.valid = true;
    esp_camera_fb_return(frame);

    ESP_LOGI(TAG,
             "PHOTO_CAPTURED id=%lu bytes=%u",
             (unsigned long)crossing_id,
             (unsigned)s_pending.jpeg_size);
    return ESP_OK;
}

void camera_service_discard(uint32_t crossing_id)
{
    if (s_pending.valid && s_pending.crossing_id == crossing_id) {
        ESP_LOGI(TAG, "PHOTO_DISCARDED id=%lu", (unsigned long)crossing_id);
        release_pending_photo();
    }
}

const pending_photo_t *camera_service_pending_photo(void)
{
    return &s_pending;
}

