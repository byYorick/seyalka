#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float first_row_offset_from_sensor_mm;
    float row_pitch_mm;
    float feed_speed_mm_s;
    float index_speed_mm_s;
    uint32_t cassette_timeout_ms;
} cassette_indexer_config_t;

esp_err_t cassette_indexer_init(const cassette_indexer_config_t *config);
esp_err_t cassette_wait_and_capture(void);
esp_err_t cassette_move_to_first_row(void);
esp_err_t cassette_advance_row(uint16_t row_index);

