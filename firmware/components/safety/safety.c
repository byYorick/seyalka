#include "safety.h"

#include "board_tinybee.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motion_adapter.h"

static const char *TAG = "safety";

static portMUX_TYPE s_safety_lock = portMUX_INITIALIZER_UNLOCKED;
static safety_state_t s_state = SAFETY_STATE_BOOT;
static safety_fault_t s_latched_fault = SAFETY_FAULT_NONE;
static safety_config_t s_cfg;
static TaskHandle_t s_safety_task;
static uint32_t s_last_heartbeat_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint32_t elapsed_ms(uint32_t now, uint32_t last)
{
    return now - last;
}

static void latch_fault(safety_fault_t fault)
{
    if (fault == SAFETY_FAULT_NONE) {
        fault = SAFETY_FAULT_REQUESTED;
    }

    portENTER_CRITICAL(&s_safety_lock);
    s_state = SAFETY_STATE_FAULT;
    if (s_latched_fault == SAFETY_FAULT_NONE) {
        s_latched_fault = fault;
    }
    portEXIT_CRITICAL(&s_safety_lock);
}

static esp_err_t stop_motion_and_outputs(safety_fault_t fallback_fault)
{
    const esp_err_t motion_err = motion_stop_all();
    const esp_err_t output_err = board_safe_outputs_off();

    if (motion_err != ESP_OK && fallback_fault == SAFETY_FAULT_NONE) {
        latch_fault(SAFETY_FAULT_MOTION_STOP_FAILED);
    }

    if (output_err != ESP_OK) {
        latch_fault(SAFETY_FAULT_OUTPUT_OFF_FAILED);
        return output_err;
    }

    return motion_err;
}

static void safety_task(void *arg)
{
    (void)arg;

    while (true) {
        const esp_err_t err = safety_check_periodic();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "periodic check failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(s_cfg.check_period_ms));
    }
}

esp_err_t safety_get_default_config(safety_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = (safety_config_t) {
        .check_period_ms = 50,
        .task_stack_bytes = 3072,
        .task_priority = 6,
        .control_watchdog_timeout_ms = 0,
    };
    return ESP_OK;
}

esp_err_t safety_init(void)
{
    return safety_init_with_config(NULL);
}

esp_err_t safety_init_with_config(const safety_config_t *config)
{
    if (s_safety_task) {
        return ESP_OK;
    }

    if (config) {
        s_cfg = *config;
    } else {
        ESP_RETURN_ON_ERROR(safety_get_default_config(&s_cfg), TAG, "default config failed");
    }

    ESP_RETURN_ON_FALSE(s_cfg.check_period_ms > 0, ESP_ERR_INVALID_ARG, TAG, "check period is zero");
    ESP_RETURN_ON_FALSE(s_cfg.task_stack_bytes >= 2048, ESP_ERR_INVALID_ARG, TAG, "safety task stack too small");

    const esp_err_t output_err = board_safe_outputs_off();
    if (output_err != ESP_OK) {
        latch_fault(SAFETY_FAULT_OUTPUT_OFF_FAILED);
        return output_err;
    }

    const uint32_t heartbeat_ms = now_ms();
    portENTER_CRITICAL(&s_safety_lock);
    s_state = SAFETY_STATE_READY;
    s_latched_fault = SAFETY_FAULT_NONE;
    s_last_heartbeat_ms = heartbeat_ms;
    portEXIT_CRITICAL(&s_safety_lock);

    const BaseType_t ok = xTaskCreate(safety_task,
                                      "safety_task",
                                      s_cfg.task_stack_bytes,
                                      NULL,
                                      s_cfg.task_priority,
                                      &s_safety_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "safety task create failed");

    ESP_LOGI(TAG,
             "safety_task started: period=%lu ms, watchdog=%lu ms",
             (unsigned long)s_cfg.check_period_ms,
             (unsigned long)s_cfg.control_watchdog_timeout_ms);
    return ESP_OK;
}

esp_err_t safety_enter_safe_state(void)
{
    latch_fault(SAFETY_FAULT_REQUESTED);
    return stop_motion_and_outputs(SAFETY_FAULT_REQUESTED);
}

esp_err_t safety_check_periodic(void)
{
    board_status_snapshot_t snapshot = { 0 };
    ESP_RETURN_ON_ERROR(board_get_status_snapshot(&snapshot), TAG, "safety snapshot failed");

    if (snapshot.outputs[BOARD_OUT_VACUUM_VALVE] && snapshot.outputs[BOARD_OUT_PRESSURE_VALVE]) {
        ESP_LOGE(TAG, "vacuum and pressure valves are active at the same time");
        latch_fault(SAFETY_FAULT_VACUUM_PRESSURE_CONFLICT);
        return stop_motion_and_outputs(SAFETY_FAULT_VACUUM_PRESSURE_CONFLICT);
    }

    safety_status_t status = { 0 };
    ESP_RETURN_ON_ERROR(safety_get_status(&status), TAG, "safety status failed");
    if (status.state == SAFETY_STATE_READY &&
        status.control_watchdog_timeout_ms > 0 &&
        elapsed_ms(now_ms(), status.last_heartbeat_ms) > status.control_watchdog_timeout_ms) {
        ESP_LOGE(TAG, "control watchdog timeout");
        latch_fault(SAFETY_FAULT_CONTROL_WATCHDOG_TIMEOUT);
        return stop_motion_and_outputs(SAFETY_FAULT_CONTROL_WATCHDOG_TIMEOUT);
    }

    return ESP_OK;
}

esp_err_t safety_ack_fault(void)
{
    safety_state_t state = SAFETY_STATE_BOOT;
    portENTER_CRITICAL(&s_safety_lock);
    state = s_state;
    portEXIT_CRITICAL(&s_safety_lock);

    if (state != SAFETY_STATE_FAULT && state != SAFETY_STATE_ESTOP) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_safe_outputs_off(), TAG, "ack output off failed");

    const uint32_t heartbeat_ms = now_ms();
    portENTER_CRITICAL(&s_safety_lock);
    s_state = SAFETY_STATE_READY;
    s_latched_fault = SAFETY_FAULT_NONE;
    s_last_heartbeat_ms = heartbeat_ms;
    portEXIT_CRITICAL(&s_safety_lock);

    ESP_LOGI(TAG, "fault acknowledged");
    return ESP_OK;
}

esp_err_t safety_control_heartbeat(void)
{
    const uint32_t heartbeat_ms = now_ms();
    portENTER_CRITICAL(&s_safety_lock);
    s_last_heartbeat_ms = heartbeat_ms;
    portEXIT_CRITICAL(&s_safety_lock);
    return ESP_OK;
}

esp_err_t safety_configure_watchdog(uint32_t timeout_ms)
{
    const uint32_t heartbeat_ms = now_ms();
    portENTER_CRITICAL(&s_safety_lock);
    s_cfg.control_watchdog_timeout_ms = timeout_ms;
    s_last_heartbeat_ms = heartbeat_ms;
    portEXIT_CRITICAL(&s_safety_lock);

    ESP_LOGI(TAG, "control watchdog timeout=%lu ms", (unsigned long)timeout_ms);
    return ESP_OK;
}

bool safety_can_start_cycle(void)
{
    safety_status_t status = { 0 };
    if (safety_get_status(&status) != ESP_OK) {
        return false;
    }

    return status.state == SAFETY_STATE_READY && status.latched_fault == SAFETY_FAULT_NONE;
}

safety_state_t safety_get_state(void)
{
    portENTER_CRITICAL(&s_safety_lock);
    const safety_state_t state = s_state;
    portEXIT_CRITICAL(&s_safety_lock);
    return state;
}

safety_fault_t safety_get_latched_fault(void)
{
    portENTER_CRITICAL(&s_safety_lock);
    const safety_fault_t fault = s_latched_fault;
    portEXIT_CRITICAL(&s_safety_lock);
    return fault;
}

esp_err_t safety_get_status(safety_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_safety_lock);
    status->state = s_state;
    status->latched_fault = s_latched_fault;
    status->last_heartbeat_ms = s_last_heartbeat_ms;
    status->control_watchdog_timeout_ms = s_cfg.control_watchdog_timeout_ms;
    portEXIT_CRITICAL(&s_safety_lock);
    return ESP_OK;
}
