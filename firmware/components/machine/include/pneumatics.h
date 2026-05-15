#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t vacuum_valve_on_delay_ms;
    uint32_t pressure_pulse_ms;
    uint32_t valve_deadtime_ms;
} pneumatics_config_t;

esp_err_t pneumatics_init(const pneumatics_config_t *config);
esp_err_t pneumatics_all_off(void);
esp_err_t pneumatics_vacuum_on(void);
esp_err_t pneumatics_vacuum_off(void);
esp_err_t pneumatics_pressure_pulse(void);

