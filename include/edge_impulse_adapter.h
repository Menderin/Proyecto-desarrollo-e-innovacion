#ifndef EDGE_IMPULSE_ADAPTER_H
#define EDGE_IMPULSE_ADAPTER_H

#include "acquisition.h"
#include "classifier.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t edge_impulse_adapter_init(void);
classifier_result_t edge_impulse_adapter_run(const acquisition_buffer_t *buffer);

#ifdef __cplusplus
}
#endif

#endif
