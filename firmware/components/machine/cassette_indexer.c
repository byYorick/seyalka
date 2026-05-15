#include "cassette_indexer.h"

#include "board_tinybee.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motion_adapter.h"

static const char *TAG = "cassette_indexer";
static const float FEED_CHUNK_MM = 5.0f;

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
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(s_cfg.cassette_timeout_ms);

    ESP_LOGI(TAG, "feed conveyor until cassette sensor; timeout=%lu ms",
             (unsigned long)s_cfg.cassette_timeout_ms);

    ESP_RETURN_ON_ERROR(motion_enable_all(true), TAG, "conveyor enable failed");

    if (board_get_input(BOARD_IN_CASSETTE_SENSOR)) {
        ESP_LOGI(TAG, "cassette sensor already active");
        return ESP_OK;
    }

    while (!board_get_input(BOARD_IN_CASSETTE_SENSOR)) {
        if (xTaskGetTickCount() >= deadline) {
            (void)motion_stop_all();
            ESP_LOGE(TAG, "cassette feed timeout");
            return ESP_ERR_TIMEOUT;
        }

        ESP_RETURN_ON_ERROR(motion_move_rel_mm(MOTION_AXIS_CONVEYOR, FEED_CHUNK_MM, s_cfg.feed_speed_mm_s),
                            TAG,
                            "feed chunk failed");
        ESP_RETURN_ON_ERROR(motion_wait_idle(s_cfg.cassette_timeout_ms), TAG, "feed chunk wait failed");
    }

    (void)motion_stop_all();
    ESP_LOGI(TAG, "cassette captured on sensor");
    return ESP_OK;
}

esp_err_t cassette_move_to_first_row(void)
{
    ESP_RETURN_ON_ERROR(motion_move_rel_mm(MOTION_AXIS_CONVEYOR,
                                          s_cfg.first_row_offset_from_sensor_mm,
                                          s_cfg.index_speed_mm_s),
                      TAG,
                      "first row move failed");
    return motion_wait_idle(s_cfg.cassette_timeout_ms);
}

esp_err_t cassette_advance_row(uint16_t row_index)
{
    ESP_LOGI(TAG, "advance to row %u", row_index);
    ESP_RETURN_ON_ERROR(motion_move_rel_mm(MOTION_AXIS_CONVEYOR, s_cfg.row_pitch_mm, s_cfg.index_speed_mm_s),
                      TAG,
                      "row advance failed");
    return motion_wait_idle(s_cfg.cassette_timeout_ms);
}
