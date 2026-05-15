#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float safe_position_mm;
    float pick_position_mm;
    float drop_position_mm;
    float blow_position_mm;
    float move_speed_mm_s;
    uint32_t home_timeout_ms;
} transfer_arm_config_t;

esp_err_t transfer_arm_init(const transfer_arm_config_t *config);
esp_err_t transfer_arm_home(void);
esp_err_t transfer_arm_to_pick(void);
esp_err_t transfer_arm_to_drop(void);
esp_err_t transfer_arm_to_blow(void);
esp_err_t transfer_arm_to_safe(void);

