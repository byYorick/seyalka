#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "fault_manager.h"
#include "sower_fsm.h"

typedef struct {
    uint32_t ts_ms;
    uint16_t row;
    sower_state_t state;
    fault_code_t fault;
    uint16_t vacuum_adc;
    int32_t value;
} event_log_record_t;

esp_err_t event_log_init(void);
esp_err_t event_log_push(const event_log_record_t *record);

