#pragma once

#include "esp_err.h"
#include "sower_fsm.h"

esp_err_t ui_display_init(void);
esp_err_t ui_display_update(const sower_fsm_t *fsm);

