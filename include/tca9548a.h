#ifndef TCA9548A_H
#define TCA9548A_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

esp_err_t tca9548a_select_channel(uint8_t channel);
esp_err_t tca9548a_disable_all(void);
void tca9548a_scan_channels(uint8_t first_channel, uint8_t channel_count);

#ifdef __cplusplus
}
#endif

#endif
