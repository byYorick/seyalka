#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Адреса UART на шине TinyBee (MS1/MS2): X=0, Y=1, Z=2, E0=3. */
typedef enum {
    TMC2209_ADDR_X = 0,
    TMC2209_ADDR_Y = 1,
    TMC2209_ADDR_Z = 2,
    TMC2209_ADDR_E0 = 3,
} tmc2209_addr_t;

typedef struct {
    uint16_t run_current_ma;
    uint16_t hold_current_ma;
    uint16_t microsteps;
    bool stealthchop;
    float rsense_ohm;
} tmc2209_axis_config_t;

typedef struct {
    tmc2209_axis_config_t conveyor;
    tmc2209_axis_config_t dibbler;
    tmc2209_axis_config_t transfer;
} tmc2209_config_t;

esp_err_t tmc2209_init(void);
esp_err_t tmc2209_deinit(void);
esp_err_t tmc2209_configure_axis(tmc2209_addr_t addr, const tmc2209_axis_config_t *config);
esp_err_t tmc2209_configure_all(const tmc2209_config_t *config);
esp_err_t tmc2209_get_default_config(tmc2209_config_t *config);
esp_err_t tmc2209_read_register(tmc2209_addr_t addr, uint8_t reg, uint32_t *value);
esp_err_t tmc2209_verify_axis(tmc2209_addr_t addr);
esp_err_t tmc2209_verify_sower_axes(void);
