#ifndef I2C_BUS_H
#define I2C_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

esp_err_t i2c_bus_init(void);
esp_err_t i2c_bus_probe(uint8_t addr, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
