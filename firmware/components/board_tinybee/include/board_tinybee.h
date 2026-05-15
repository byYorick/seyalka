#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "board_pins.h"

typedef enum {
    BOARD_OUT_VACUUM_PUMP = 0,
    BOARD_OUT_VACUUM_VALVE,
    BOARD_OUT_PRESSURE_VALVE,
    BOARD_OUT_VIBRO_PWM,
    BOARD_OUT_SPARE,
    BOARD_OUT_BEEPER,
    BOARD_OUT_COUNT,
} board_output_t;

typedef enum {
    BOARD_IN_CASSETTE_SENSOR = 0,
    BOARD_IN_DIBBLER_HOME,
    BOARD_IN_TRANSFER_HOME,
    BOARD_IN_START_STOP,
    BOARD_IN_ENCODER_A,
    BOARD_IN_ENCODER_B,
    BOARD_IN_ENCODER_BUTTON,
    BOARD_IN_COUNT,
} board_input_t;

typedef struct {
    bool raw[BOARD_IN_COUNT];
    bool stable[BOARD_IN_COUNT];
    uint32_t last_change_ms[BOARD_IN_COUNT];
    uint32_t updated_ms;
} board_input_snapshot_t;

typedef struct {
    board_input_snapshot_t inputs;
    bool outputs[BOARD_OUT_COUNT];
    uint16_t pwm_permille[BOARD_OUT_COUNT];
    uint16_t vacuum_adc_raw;
    uint32_t vacuum_adc_updated_ms;
    bool vacuum_adc_valid;
} board_status_snapshot_t;

esp_err_t board_init(void);
esp_err_t board_safe_outputs_off(void);
esp_err_t board_set_output(board_output_t out, bool on);
esp_err_t board_set_pwm(board_output_t out, uint16_t duty_permille);
esp_err_t board_update_inputs(void);
bool board_get_input(board_input_t in);
bool board_get_input_raw(board_input_t in);
esp_err_t board_update_vacuum_adc(void);
esp_err_t board_read_vacuum_adc(uint16_t *adc_raw);
esp_err_t board_get_status_snapshot(board_status_snapshot_t *snapshot);
