#include "io_service.h"

#include "board_tinybee.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "io_service";

static TaskHandle_t s_io_task;
static io_service_config_t s_cfg;

#if CONFIG_SOWER_IO_MONITOR
static const char *input_name(board_input_t input)
{
    switch (input) {
    case BOARD_IN_CASSETTE_SENSOR: return "cassette";
    case BOARD_IN_DIBBLER_HOME: return "dibbler_home";
    case BOARD_IN_TRANSFER_HOME: return "transfer_home";
    case BOARD_IN_START_STOP: return "start_stop";
    case BOARD_IN_ENCODER_A: return "enc_a";
    case BOARD_IN_ENCODER_B: return "enc_b";
    case BOARD_IN_ENCODER_BUTTON: return "enc_btn";
    default: return "unknown";
    }
}

static void log_io_monitor(const board_status_snapshot_t *snapshot)
{
    ESP_LOGI(TAG,
             "vacuum_adc raw=%u valid=%d updated=%lu ms",
             snapshot->vacuum_adc_raw,
             snapshot->vacuum_adc_valid,
             (unsigned long)snapshot->vacuum_adc_updated_ms);

    for (int i = 0; i < BOARD_IN_COUNT; ++i) {
        ESP_LOGI(TAG,
                 "input %-13s stable=%d raw=%d last_change=%lu ms",
                 input_name((board_input_t)i),
                 snapshot->inputs.stable[i],
                 snapshot->inputs.raw[i],
                 (unsigned long)snapshot->inputs.last_change_ms[i]);
    }
}
#endif

esp_err_t io_service_get_default_config(io_service_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = (io_service_config_t) {
        .input_period_ms = 10,
        .vacuum_adc_period_ms = 20,
        .task_stack_bytes = 3072,
        .task_priority = 5,
    };
    return ESP_OK;
}

static uint32_t elapsed_ms(uint32_t now_ms, uint32_t last_ms)
{
    return now_ms - last_ms;
}

static void io_task(void *arg)
{
    (void)arg;

    uint32_t last_input_ms = 0;
    uint32_t last_adc_ms = 0;
#if CONFIG_SOWER_IO_MONITOR
    uint32_t last_monitor_ms = 0;
#endif

    while (true) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (elapsed_ms(now_ms, last_input_ms) >= s_cfg.input_period_ms) {
            const esp_err_t err = board_update_inputs();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "input update failed: %s", esp_err_to_name(err));
            }
            last_input_ms = now_ms;
        }

        if (elapsed_ms(now_ms, last_adc_ms) >= s_cfg.vacuum_adc_period_ms) {
            const esp_err_t err = board_update_vacuum_adc();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "vacuum adc update failed: %s", esp_err_to_name(err));
            }
            last_adc_ms = now_ms;
        }

#if CONFIG_SOWER_IO_MONITOR
        if (elapsed_ms(now_ms, last_monitor_ms) >= CONFIG_SOWER_IO_MONITOR_PERIOD_MS) {
            board_status_snapshot_t snapshot = { 0 };
            const esp_err_t err = board_get_status_snapshot(&snapshot);
            if (err == ESP_OK) {
                log_io_monitor(&snapshot);
            } else {
                ESP_LOGW(TAG, "io monitor snapshot failed: %s", esp_err_to_name(err));
            }
            last_monitor_ms = now_ms;
        }
#endif

        /* io_task задает частоту опроса board layer; вся тяжелая логика остается вне этой задачи. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t io_service_start(const io_service_config_t *config)
{
    if (s_io_task) {
        return ESP_OK;
    }

    if (config) {
        s_cfg = *config;
    } else {
        ESP_RETURN_ON_ERROR(io_service_get_default_config(&s_cfg), TAG, "default config failed");
    }

    ESP_RETURN_ON_FALSE(s_cfg.input_period_ms > 0, ESP_ERR_INVALID_ARG, TAG, "input period is zero");
    ESP_RETURN_ON_FALSE(s_cfg.vacuum_adc_period_ms > 0, ESP_ERR_INVALID_ARG, TAG, "adc period is zero");
    ESP_RETURN_ON_FALSE(s_cfg.task_stack_bytes >= 2048, ESP_ERR_INVALID_ARG, TAG, "io task stack too small");

    const BaseType_t ok = xTaskCreate(io_task,
                                      "io_task",
                                      s_cfg.task_stack_bytes,
                                      NULL,
                                      s_cfg.task_priority,
                                      &s_io_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "io task create failed");

    ESP_LOGI(TAG, "io_task started: inputs=%lu ms, vacuum_adc=%lu ms",
             (unsigned long)s_cfg.input_period_ms,
             (unsigned long)s_cfg.vacuum_adc_period_ms);
#if CONFIG_SOWER_IO_MONITOR
    ESP_LOGW(TAG, "io monitor is enabled, period=%d ms", CONFIG_SOWER_IO_MONITOR_PERIOD_MS);
#endif
    return ESP_OK;
}
