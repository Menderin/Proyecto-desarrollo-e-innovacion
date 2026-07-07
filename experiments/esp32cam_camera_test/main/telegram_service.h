#ifndef TELEGRAM_SERVICE_H
#define TELEGRAM_SERVICE_H

#include <stdint.h>

#include "camera_service.h"
#include "esp_err.h"

typedef struct {
    pending_photo_t photo;
    int32_t p90_raw;
    int32_t threshold_raw;
} telegram_alert_t;

esp_err_t telegram_service_init(void);
esp_err_t telegram_service_enqueue(telegram_alert_t *alert);

#endif

