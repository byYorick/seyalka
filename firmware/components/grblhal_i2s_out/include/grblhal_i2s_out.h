#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define GRBLHAL_I2S_OUT_NUM_BITS 32U

typedef struct {
    uint8_t ws_gpio;
    uint8_t bck_gpio;
    uint8_t data_gpio;
    uint32_t init_value;
} grblhal_i2s_out_config_t;

esp_err_t grblhal_i2s_out_init(const grblhal_i2s_out_config_t *config);
esp_err_t grblhal_i2s_out_write(uint8_t bit, bool value);
bool grblhal_i2s_out_state(uint8_t bit);
void grblhal_i2s_out_delay(void);
