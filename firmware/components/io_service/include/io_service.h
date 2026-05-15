#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t input_period_ms;
    uint32_t vacuum_adc_period_ms;
    uint32_t task_stack_bytes;
    uint8_t task_priority;
} io_service_config_t;

esp_err_t io_service_start(const io_service_config_t *config);
esp_err_t io_service_get_default_config(io_service_config_t *config);
