#ifndef CAMERA_SERVICE_H
#define CAMERA_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t *jpeg;
    size_t jpeg_size;
    uint32_t crossing_id;
    char direction[20];
    bool valid;
} pending_photo_t;

esp_err_t camera_service_init(void);
esp_err_t camera_service_capture(uint32_t crossing_id, const char *direction);
void camera_service_discard(uint32_t crossing_id);
const pending_photo_t *camera_service_pending_photo(void);
esp_err_t camera_service_take_pending(uint32_t crossing_id, pending_photo_t *photo);
void camera_service_release_photo(pending_photo_t *photo);

#endif
