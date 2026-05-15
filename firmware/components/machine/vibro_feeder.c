#include "vibro_feeder.h"

#include "board_tinybee.h"

static vibro_feeder_config_t s_cfg;

esp_err_t vibro_feeder_init(const vibro_feeder_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;
    return vibro_feeder_set_pick(false);
}

esp_err_t vibro_feeder_set_pick(bool on)
{
    if (!s_cfg.enabled || !on) {
        return board_set_pwm(BOARD_OUT_VIBRO_PWM, 0);
    }
    return board_set_pwm(BOARD_OUT_VIBRO_PWM, s_cfg.pwm_duty_pick_permille);
}

