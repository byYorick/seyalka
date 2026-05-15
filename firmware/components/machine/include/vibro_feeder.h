#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool enabled;
    uint16_t pwm_duty_pick_permille;
    uint32_t pre_pick_ms;
    uint32_t post_pick_ms;
} vibro_feeder_config_t;

esp_err_t vibro_feeder_init(const vibro_feeder_config_t *config);
esp_err_t vibro_feeder_set_pick(bool on);

