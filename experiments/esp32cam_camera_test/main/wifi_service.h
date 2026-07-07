#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t wifi_service_init(void);
bool wifi_service_wait_connected(TickType_t timeout_ticks);
esp_err_t wifi_service_sync_time(TickType_t timeout_ticks);

#endif
