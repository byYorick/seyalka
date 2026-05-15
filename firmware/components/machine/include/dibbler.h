#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float safe_position_mm;
    float work_position_mm;
    float move_speed_mm_s;
    uint32_t dwell_ms;
    uint32_t home_timeout_ms;
} dibbler_config_t;

esp_err_t dibbler_init(const dibbler_config_t *config);
esp_err_t dibbler_home(void);
esp_err_t dibbler_make_holes(void);

