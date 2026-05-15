#include "vacuum_system.h"

#include "board_tinybee.h"

static vacuum_config_t s_cfg;

esp_err_t vacuum_system_init(const vacuum_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;
    return ESP_OK;
}

esp_err_t vacuum_read_raw(uint16_t *adc_raw)
{
    return board_read_vacuum_adc(adc_raw);
}

vacuum_result_t vacuum_check_pickup(void)
{
    uint16_t adc = 0;
    if (vacuum_read_raw(&adc) != ESP_OK) {
        return VACUUM_SENSOR_RANGE_ERROR;
    }
    return adc >= s_cfg.pickup_threshold_adc ? VACUUM_OK : VACUUM_TIMEOUT;
}

vacuum_result_t vacuum_check_release(void)
{
    uint16_t adc = 0;
    if (vacuum_read_raw(&adc) != ESP_OK) {
        return VACUUM_SENSOR_RANGE_ERROR;
    }
    return adc <= s_cfg.release_threshold_adc ? VACUUM_OK : VACUUM_RELEASE_FAILED;
}

