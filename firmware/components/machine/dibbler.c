#include "dibbler.h"

#include "esp_check.h"
#include "motion_adapter.h"
#include "esp_rom_sys.h"

static dibbler_config_t s_cfg;

esp_err_t dibbler_init(const dibbler_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;
    return ESP_OK;
}

esp_err_t dibbler_home(void)
{
    return motion_home_axis(MOTION_AXIS_DIBBLER, s_cfg.home_timeout_ms);
}

esp_err_t dibbler_make_holes(void)
{
    ESP_RETURN_ON_ERROR(motion_move_abs_mm(MOTION_AXIS_DIBBLER, s_cfg.work_position_mm, s_cfg.move_speed_mm_s),
                        "dibbler",
                        "work move failed");
    ESP_RETURN_ON_ERROR(motion_wait_idle(s_cfg.home_timeout_ms), "dibbler", "work move wait failed");

    if (s_cfg.dwell_ms > 0) {
        esp_rom_delay_us(s_cfg.dwell_ms * 1000U);
    }

    ESP_RETURN_ON_ERROR(motion_move_abs_mm(MOTION_AXIS_DIBBLER, s_cfg.safe_position_mm, s_cfg.move_speed_mm_s),
                        "dibbler",
                        "safe move failed");
    return motion_wait_idle(s_cfg.home_timeout_ms);
}
