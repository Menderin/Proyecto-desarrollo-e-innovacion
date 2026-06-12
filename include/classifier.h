#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include "acquisition.h"
#include "esp_err.h"

typedef enum {
    CLASSIFIER_RESULT_EMPTY = 0,
    CLASSIFIER_RESULT_UNKNOWN_METAL,
} classifier_result_t;

esp_err_t classifier_init(void);
classifier_result_t classifier_run(const acquisition_buffer_t *buffer);
const char *classifier_result_to_string(classifier_result_t result);

#endif
