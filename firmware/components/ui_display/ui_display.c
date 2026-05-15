#include "ui_display.h"

#include "esp_log.h"

static const char *TAG = "ui_display";

esp_err_t ui_display_init(void)
{
    ESP_LOGW(TAG, "ILI9341 backend is a stub");
    return ESP_OK;
}

esp_err_t ui_display_update(const sower_fsm_t *fsm)
{
    if (!fsm) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "state=%s row=%u", sower_state_name(fsm->state), fsm->current_row);
    return ESP_OK;
}

