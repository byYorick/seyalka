#include "event_log.h"

#include "esp_log.h"

static const char *TAG = "event_log";

esp_err_t event_log_init(void)
{
    return ESP_OK;
}

esp_err_t event_log_push(const event_log_record_t *record)
{
    if (!record) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "row=%u state=%d fault=%d value=%ld",
             record->row, record->state, record->fault, (long)record->value);
    return ESP_OK;
}

