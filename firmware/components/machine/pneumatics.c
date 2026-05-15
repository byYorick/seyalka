#include "pneumatics.h"

#include "board_tinybee.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static pneumatics_config_t s_cfg;

esp_err_t pneumatics_init(const pneumatics_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;
    return pneumatics_all_off();
}

esp_err_t pneumatics_all_off(void)
{
    ESP_RETURN_ON_ERROR(board_set_output(BOARD_OUT_PRESSURE_VALVE, false), "pneumatics", "pressure off failed");
    return board_set_output(BOARD_OUT_VACUUM_VALVE, false);
}

esp_err_t pneumatics_vacuum_on(void)
{
    ESP_RETURN_ON_ERROR(board_set_output(BOARD_OUT_PRESSURE_VALVE, false), "pneumatics", "pressure off failed");
    if (s_cfg.valve_deadtime_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(s_cfg.valve_deadtime_ms));
    }
    return board_set_output(BOARD_OUT_VACUUM_VALVE, true);
}

esp_err_t pneumatics_vacuum_off(void)
{
    return board_set_output(BOARD_OUT_VACUUM_VALVE, false);
}

esp_err_t pneumatics_pressure_pulse(void)
{
    ESP_RETURN_ON_ERROR(board_set_output(BOARD_OUT_VACUUM_VALVE, false), "pneumatics", "vacuum off failed");
    if (s_cfg.valve_deadtime_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(s_cfg.valve_deadtime_ms));
    }
    ESP_RETURN_ON_ERROR(board_set_output(BOARD_OUT_PRESSURE_VALVE, true), "pneumatics", "pressure on failed");
    vTaskDelay(pdMS_TO_TICKS(s_cfg.pressure_pulse_ms));
    return board_set_output(BOARD_OUT_PRESSURE_VALVE, false);
}
