#include "app_context.h"

#include "board_tinybee.h"
#include "config_store.h"
#include "esp_check.h"
#include "esp_log.h"
#include "event_log.h"
#include "io_service.h"
#if CONFIG_SOWER_OUTPUT_LED_TEST
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "safety.h"
#include "ui_display.h"
#include "ui_web.h"

static const char *TAG = "app_main";
static app_context_t s_app;

#if CONFIG_SOWER_OUTPUT_LED_TEST
typedef struct {
    board_output_t output;
    const char *name;
} output_led_test_step_t;

static const output_led_test_step_t s_output_led_test[] = {
    { BOARD_OUT_VACUUM_PUMP, "H-BED" },
    { BOARD_OUT_VACUUM_VALVE, "H-E0" },
    { BOARD_OUT_PRESSURE_VALVE, "H-E1" },
    { BOARD_OUT_VIBRO_PWM, "FAN1" },
    { BOARD_OUT_SPARE, "FAN2" },
};

static void output_led_test_task(void *arg)
{
    (void)arg;

    while (true) {
        for (size_t i = 0; i < sizeof(s_output_led_test) / sizeof(s_output_led_test[0]); ++i) {
            ESP_LOGI(TAG, "output led test: %s on", s_output_led_test[i].name);
            ESP_ERROR_CHECK(board_safe_outputs_off());
            ESP_ERROR_CHECK(board_set_output(s_output_led_test[i].output, true));
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP_ERROR_CHECK(board_set_output(s_output_led_test[i].output, false));
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "sower TinyBee firmware skeleton boot");

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(config_store_load_recipe(&s_app.recipe));

    if (!recipe_validate(&s_app.recipe)) {
        ESP_LOGE(TAG, "invalid recipe");
        return;
    }

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(io_service_start(NULL));
    ESP_ERROR_CHECK(safety_init());
    ESP_ERROR_CHECK(event_log_init());
    ESP_ERROR_CHECK(ui_display_init());
    ESP_ERROR_CHECK(ui_web_init());
    ESP_ERROR_CHECK(sower_fsm_init(&s_app.fsm, &s_app.recipe));

    ESP_ERROR_CHECK(ui_display_update(&s_app.fsm));
#if CONFIG_SOWER_OUTPUT_LED_TEST
    ESP_LOGW(TAG, "output LED test is enabled: H-BED, H-E0, H-E1, FAN1 and FAN2 will toggle continuously");
    xTaskCreate(output_led_test_task, "output_led_test", 3072, NULL, 2, NULL);
#else
    ESP_LOGI(TAG, "output LED test is disabled; outputs remain under normal safety control");
#endif
    ESP_LOGI(TAG, "skeleton initialized; motion, display and web backends are still stubs");
}
