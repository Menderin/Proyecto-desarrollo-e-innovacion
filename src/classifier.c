#include "classifier.h"

#include <stdlib.h>

#include "edge_impulse_adapter.h"

esp_err_t classifier_init(void)
{
    return edge_impulse_adapter_init();
}

classifier_result_t classifier_run(const acquisition_buffer_t *buffer)
{
    return edge_impulse_adapter_run(buffer);
}

const char *classifier_result_to_string(classifier_result_t result)
{
    switch (result) {
    case CLASSIFIER_RESULT_EMPTY:
        return "vacio";
    case CLASSIFIER_RESULT_SAFE:
        return "seguro";
    case CLASSIFIER_RESULT_ALERT:
        return "alerta";
    default:
        return "resultado_invalido";
    }
}
