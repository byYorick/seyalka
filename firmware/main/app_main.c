#include "app_context.h"

#include "board_tinybee.h"
#include "config_store.h"
#include "esp_check.h"
#include "esp_log.h"
#include "event_log.h"
#include "io_service.h"
#include "motion_adapter.h"
#if CONFIG_SOWER_OUTPUT_LED_TEST || CONFIG_SOWER_CONVEYOR_MOTION_TEST
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

#if CONFIG_SOWER_CONVEYOR_MOTION_TEST
static void conveyor_motion_test_task(void *arg)
{
    (void)arg;

    const float distance_mm = (float)CONFIG_SOWER_CONVEYOR_MOTION_TEST_DISTANCE_MM;
    const float speed_mm_s = (float)CONFIG_SOWER_CONVEYOR_MOTION_TEST_SPEED_MM_S;
    const uint32_t move_timeout_ms = 120000;

    vTaskDelay(pdMS_TO_TICKS(500));

    while (true) {
        ESP_LOGI(TAG, "conveyor motion test: forward %.1f mm @ %.1f mm/s", distance_mm, speed_mm_s);
        ESP_ERROR_CHECK(motion_enable_all(true));
        ESP_ERROR_CHECK(motion_move_rel_mm(MOTION_AXIS_CONVEYOR, distance_mm, speed_mm_s));
        ESP_ERROR_CHECK(motion_wait_idle(move_timeout_ms));

        vTaskDelay(pdMS_TO_TICKS(300));

        ESP_LOGI(TAG, "conveyor motion test: reverse %.1f mm @ %.1f mm/s", distance_mm, speed_mm_s);
        ESP_ERROR_CHECK(motion_move_rel_mm(MOTION_AXIS_CONVEYOR, -distance_mm, speed_mm_s));
        ESP_ERROR_CHECK(motion_wait_idle(move_timeout_ms));
        ESP_ERROR_CHECK(motion_enable_all(false));

        vTaskDelay(pdMS_TO_TICKS(1000));
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
#if CONFIG_SOWER_CONVEYOR_MOTION_TEST
    ESP_LOGW(TAG,
             "conveyor motion test is enabled: X axis will move +/- %d mm at %d mm/s",
             CONFIG_SOWER_CONVEYOR_MOTION_TEST_DISTANCE_MM,
             CONFIG_SOWER_CONVEYOR_MOTION_TEST_SPEED_MM_S);
    xTaskCreate(conveyor_motion_test_task, "conveyor_motion_test", 4096, NULL, 3, NULL);
#endif
    ESP_LOGI(TAG, "boot complete; display and web backends are still stubs");
}
