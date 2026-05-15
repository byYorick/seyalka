#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    VACUUM_OK = 0,
    VACUUM_TIMEOUT,
    VACUUM_SENSOR_RANGE_ERROR,
    VACUUM_RELEASE_FAILED,
} vacuum_result_t;

typedef struct {
    uint16_t pickup_threshold_adc;
    uint16_t hold_min_adc;
    uint16_t release_threshold_adc;
    uint32_t pickup_timeout_ms;
    uint8_t retry_count;
} vacuum_config_t;

esp_err_t vacuum_system_init(const vacuum_config_t *config);
esp_err_t vacuum_read_raw(uint16_t *adc_raw);
vacuum_result_t vacuum_check_pickup(void);
vacuum_result_t vacuum_check_release(void);

