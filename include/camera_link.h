#ifndef CAMERA_LINK_H
#define CAMERA_LINK_H

#include <stdint.h>

#include "esp_err.h"

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t camera_link_init(void);
void camera_link_send_ping(void);
void camera_link_send_start(uint32_t crossing_id, const char *direction);
void camera_link_send_safe(uint32_t crossing_id);
void camera_link_send_alert(uint32_t crossing_id, int32_t p90_raw, int32_t threshold_raw);

#ifdef __cplusplus
}
#endif

#endif
