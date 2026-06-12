#ifndef IR_SENSOR_H
#define IR_SENSOR_H

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool ir1_detected;
    bool ir2_detected;
} ir_state_t;

esp_err_t ir_sensor_init(void);
ir_state_t ir_sensor_read(void);
bool ir_sensor_any_detected(ir_state_t state);

#endif
