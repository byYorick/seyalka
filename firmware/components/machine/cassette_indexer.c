#include "cassette_indexer.h"

#include "board_tinybee.h"
#include "motion_adapter.h"
#include "esp_log.h"

static const char *TAG = "cassette_indexer";
static cassette_indexer_config_t s_cfg;

esp_err_t cassette_indexer_init(const cassette_indexer_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;
    return ESP_OK;
}

esp_err_t cassette_wait_and_capture(void)
{
    ESP_LOGI(TAG, "feed conveyor until cassette sensor; timeout=%lu ms",
             (unsigned long)s_cfg.cassette_timeout_ms);
    return motion_move_rel_mm(MOTION_AXIS_CONVEYOR, 100000.0f, s_cfg.feed_speed_mm_s);
}

esp_err_t cassette_move_to_first_row(void)
{
    return motion_move_rel_mm(MOTION_AXIS_CONVEYOR,
                              s_cfg.first_row_offset_from_sensor_mm,
                              s_cfg.index_speed_mm_s);
}

esp_err_t cassette_advance_row(uint16_t row_index)
{
    ESP_LOGI(TAG, "advance to row %u", row_index);
    return motion_move_rel_mm(MOTION_AXIS_CONVEYOR, s_cfg.row_pitch_mm, s_cfg.index_speed_mm_s);
}

