#include "transfer_arm.h"

#include "esp_check.h"
#include "motion_adapter.h"

static const char *TAG = "transfer_arm";
static transfer_arm_config_t s_cfg;

static esp_err_t move_and_wait(float position_mm)
{
    ESP_RETURN_ON_ERROR(motion_move_abs_mm(MOTION_AXIS_TRANSFER, position_mm, s_cfg.move_speed_mm_s),
                        TAG,
                        "transfer move failed");
    return motion_wait_idle(s_cfg.home_timeout_ms);
}

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
    return move_and_wait(s_cfg.pick_position_mm);
}

esp_err_t transfer_arm_to_drop(void)
{
    return move_and_wait(s_cfg.drop_position_mm);
}

esp_err_t transfer_arm_to_blow(void)
{
    return move_and_wait(s_cfg.blow_position_mm);
}

esp_err_t transfer_arm_to_safe(void)
{
    return move_and_wait(s_cfg.safe_position_mm);
}

