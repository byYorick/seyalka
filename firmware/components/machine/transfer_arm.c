#include "transfer_arm.h"

#include "motion_adapter.h"

static transfer_arm_config_t s_cfg;

esp_err_t transfer_arm_init(const transfer_arm_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;
    return ESP_OK;
}

esp_err_t transfer_arm_home(void)
{
    return motion_home_axis(MOTION_AXIS_TRANSFER, s_cfg.home_timeout_ms);
}

esp_err_t transfer_arm_to_pick(void)
{
    return motion_move_abs_mm(MOTION_AXIS_TRANSFER, s_cfg.pick_position_mm, s_cfg.move_speed_mm_s);
}

esp_err_t transfer_arm_to_drop(void)
{
    return motion_move_abs_mm(MOTION_AXIS_TRANSFER, s_cfg.drop_position_mm, s_cfg.move_speed_mm_s);
}

esp_err_t transfer_arm_to_blow(void)
{
    return motion_move_abs_mm(MOTION_AXIS_TRANSFER, s_cfg.blow_position_mm, s_cfg.move_speed_mm_s);
}

esp_err_t transfer_arm_to_safe(void)
{
    return motion_move_abs_mm(MOTION_AXIS_TRANSFER, s_cfg.safe_position_mm, s_cfg.move_speed_mm_s);
}

