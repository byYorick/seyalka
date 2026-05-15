#include "motion_adapter.h"

#include <math.h>
#include <stdint.h>

#include "board_pins.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "grblhal_i2s_out.h"

static const char *TAG = "motion_adapter";
static const uint32_t MOTION_TASK_STACK_BYTES = 4096;
static const UBaseType_t MOTION_TASK_PRIORITY = 8;
static const uint32_t STEP_PULSE_WIDTH_US = 10;
static const uint32_t MIN_STEP_INTERVAL_US = 50;

typedef struct {
    uint8_t en_bit;
    uint8_t step_bit;
    uint8_t dir_bit;
    bool enable_active_low;
    bool positive_dir_level;
} axis_pin_map_t;

typedef struct {
    motion_axis_t axis;
    int32_t delta_steps;
    uint32_t interval_us;
} motion_command_t;

static const axis_pin_map_t s_axis_pins[MOTION_AXIS_COUNT] = {
    [MOTION_AXIS_CONVEYOR] = {
        .en_bit = TB_I2SO_X_EN,
        .step_bit = TB_I2SO_X_STEP,
        .dir_bit = TB_I2SO_X_DIR,
        .enable_active_low = true,
        .positive_dir_level = true,
    },
    [MOTION_AXIS_DIBBLER] = {
        .en_bit = TB_I2SO_Z_EN,
        .step_bit = TB_I2SO_Z_STEP,
        .dir_bit = TB_I2SO_Z_DIR,
        .enable_active_low = true,
        .positive_dir_level = true,
    },
    [MOTION_AXIS_TRANSFER] = {
        .en_bit = TB_I2SO_E0_EN,
        .step_bit = TB_I2SO_E0_STEP,
        .dir_bit = TB_I2SO_E0_DIR,
        .enable_active_low = true,
        .positive_dir_level = true,
    },
};

static portMUX_TYPE s_motion_lock = portMUX_INITIALIZER_UNLOCKED;
static motion_config_t s_cfg;
static QueueHandle_t s_motion_queue;
static TaskHandle_t s_motion_task;
static bool s_initialized;
static bool s_enabled;
static bool s_busy;
static bool s_stop_requested;
static motion_axis_t s_active_axis = MOTION_AXIS_COUNT;
static int32_t s_position_steps[MOTION_AXIS_COUNT];

static motion_axis_cfg_t *axis_cfg(motion_axis_t axis)
{
    switch (axis) {
    case MOTION_AXIS_CONVEYOR: return &s_cfg.conveyor;
    case MOTION_AXIS_DIBBLER: return &s_cfg.dibbler;
    case MOTION_AXIS_TRANSFER: return &s_cfg.transfer;
    default: return NULL;
    }
}

static bool axis_valid(motion_axis_t axis)
{
    return axis >= MOTION_AXIS_CONVEYOR && axis < MOTION_AXIS_COUNT;
}

static esp_err_t validate_axis_config(const motion_axis_cfg_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "axis config is null");
    ESP_RETURN_ON_FALSE(config->steps_per_mm > 0.0f, ESP_ERR_INVALID_ARG, TAG, "steps_per_mm must be positive");
    ESP_RETURN_ON_FALSE(config->max_speed_mm_s > 0.0f, ESP_ERR_INVALID_ARG, TAG, "max_speed must be positive");
    ESP_RETURN_ON_FALSE(config->accel_mm_s2 >= 0.0f, ESP_ERR_INVALID_ARG, TAG, "accel must be non-negative");
    return ESP_OK;
}

static esp_err_t write_axis_enable(motion_axis_t axis, bool enable)
{
    if (!axis_valid(axis)) {
        return ESP_ERR_INVALID_ARG;
    }

    const axis_pin_map_t *pins = &s_axis_pins[axis];
    const bool level = pins->enable_active_low ? !enable : enable;
    ESP_RETURN_ON_ERROR(grblhal_i2s_out_write(pins->en_bit, level), TAG, "enable write failed");
    grblhal_i2s_out_delay();
    return ESP_OK;
}

static esp_err_t write_axis_step(motion_axis_t axis, bool level)
{
    if (!axis_valid(axis)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(grblhal_i2s_out_write(s_axis_pins[axis].step_bit, level), TAG, "step write failed");
    grblhal_i2s_out_delay();
    return ESP_OK;
}

static esp_err_t write_axis_dir(motion_axis_t axis, bool positive)
{
    if (!axis_valid(axis)) {
        return ESP_ERR_INVALID_ARG;
    }

    const axis_pin_map_t *pins = &s_axis_pins[axis];
    const bool level = positive ? pins->positive_dir_level : !pins->positive_dir_level;
    ESP_RETURN_ON_ERROR(grblhal_i2s_out_write(pins->dir_bit, level), TAG, "dir write failed");
    grblhal_i2s_out_delay();
    return ESP_OK;
}

static bool stop_requested(void)
{
    portENTER_CRITICAL(&s_motion_lock);
    const bool requested = s_stop_requested;
    portEXIT_CRITICAL(&s_motion_lock);
    return requested;
}

static void set_busy(bool busy, motion_axis_t axis)
{
    portENTER_CRITICAL(&s_motion_lock);
    s_busy = busy;
    s_active_axis = busy ? axis : MOTION_AXIS_COUNT;
    if (!busy) {
        s_stop_requested = false;
    }
    portEXIT_CRITICAL(&s_motion_lock);
}

static void add_position_step(motion_axis_t axis, int32_t step_delta)
{
    portENTER_CRITICAL(&s_motion_lock);
    s_position_steps[axis] += step_delta;
    portEXIT_CRITICAL(&s_motion_lock);
}

static void delay_between_steps(uint32_t interval_us)
{
    const uint32_t high_time_us = STEP_PULSE_WIDTH_US;
    if (interval_us > high_time_us) {
        esp_rom_delay_us(interval_us - high_time_us);
    }
}

static void motion_task(void *arg)
{
    (void)arg;
    motion_command_t command = { 0 };

    while (true) {
        if (xQueueReceive(s_motion_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const bool positive = command.delta_steps >= 0;
        uint32_t steps_left = (uint32_t)(positive ? command.delta_steps : -command.delta_steps);
        const int32_t step_delta = positive ? 1 : -1;

        ESP_LOGI(TAG,
                 "motion start axis=%d steps=%lu interval=%lu us",
                 command.axis,
                 (unsigned long)steps_left,
                 (unsigned long)command.interval_us);

        esp_err_t err = write_axis_enable(command.axis, true);
        if (err == ESP_OK) {
            err = write_axis_dir(command.axis, positive);
        }

        while (err == ESP_OK && steps_left > 0 && !stop_requested()) {
            err = write_axis_step(command.axis, true);
            if (err != ESP_OK) {
                break;
            }

            esp_rom_delay_us(STEP_PULSE_WIDTH_US);
            err = write_axis_step(command.axis, false);
            if (err != ESP_OK) {
                break;
            }

            add_position_step(command.axis, step_delta);
            --steps_left;
            delay_between_steps(command.interval_us);

            /* Временный bring-up генератор отдает управление RTOS, чтобы не забивать ядро длинным ходом. */
            if ((steps_left & 0x7FU) == 0U) {
                taskYIELD();
            }
        }

        (void)write_axis_step(command.axis, false);
        set_busy(false, command.axis);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "motion failed: %s", esp_err_to_name(err));
        } else if (steps_left > 0) {
            ESP_LOGW(TAG, "motion stopped axis=%d, remaining=%lu steps", command.axis, (unsigned long)steps_left);
        } else {
            ESP_LOGI(TAG, "motion done axis=%d", command.axis);
        }
    }
}

static esp_err_t mm_to_steps(motion_axis_t axis, float mm, int32_t *steps)
{
    if (!steps || !axis_valid(axis)) {
        return ESP_ERR_INVALID_ARG;
    }

    const motion_axis_cfg_t *config = axis_cfg(axis);
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "axis config not found");

    const float value = mm * config->steps_per_mm;
    ESP_RETURN_ON_FALSE(value <= (float)INT32_MAX && value >= (float)INT32_MIN,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "step target overflow");

    *steps = (int32_t)lroundf(value);
    return ESP_OK;
}

static esp_err_t queue_move_steps(motion_axis_t axis, int32_t delta_steps, float speed_mm_s)
{
    if (!axis_valid(axis)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (delta_steps == 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "motion is not initialized");
    ESP_RETURN_ON_FALSE(speed_mm_s > 0.0f, ESP_ERR_INVALID_ARG, TAG, "speed must be positive");

    const motion_axis_cfg_t *config = axis_cfg(axis);
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "axis config not found");

    const float clamped_speed_mm_s = fminf(speed_mm_s, config->max_speed_mm_s);
    const float steps_per_second = clamped_speed_mm_s * config->steps_per_mm;
    ESP_RETURN_ON_FALSE(steps_per_second > 0.0f, ESP_ERR_INVALID_ARG, TAG, "step rate must be positive");

    uint32_t interval_us = (uint32_t)lroundf(1000000.0f / steps_per_second);
    if (interval_us < MIN_STEP_INTERVAL_US) {
        ESP_LOGW(TAG, "step interval capped from %lu us to %lu us",
                 (unsigned long)interval_us,
                 (unsigned long)MIN_STEP_INTERVAL_US);
        interval_us = MIN_STEP_INTERVAL_US;
    }

    portENTER_CRITICAL(&s_motion_lock);
    const bool can_start = !s_busy;
    if (can_start) {
        s_busy = true;
        s_stop_requested = false;
        s_active_axis = axis;
    }
    portEXIT_CRITICAL(&s_motion_lock);

    ESP_RETURN_ON_FALSE(can_start, ESP_ERR_INVALID_STATE, TAG, "motion is busy");

    const motion_command_t command = {
        .axis = axis,
        .delta_steps = delta_steps,
        .interval_us = interval_us,
    };

    if (xQueueSend(s_motion_queue, &command, 0) != pdTRUE) {
        set_busy(false, axis);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t motion_init(const motion_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "motion config is null");
    ESP_RETURN_ON_ERROR(validate_axis_config(&config->conveyor), TAG, "conveyor config invalid");
    ESP_RETURN_ON_ERROR(validate_axis_config(&config->dibbler), TAG, "dibbler config invalid");
    ESP_RETURN_ON_ERROR(validate_axis_config(&config->transfer), TAG, "transfer config invalid");

    if (s_initialized) {
        s_cfg = *config;
        return ESP_OK;
    }

    s_cfg = *config;
    s_motion_queue = xQueueCreate(1, sizeof(motion_command_t));
    ESP_RETURN_ON_FALSE(s_motion_queue, ESP_ERR_NO_MEM, TAG, "motion queue allocation failed");

    BaseType_t task_ok = xTaskCreatePinnedToCore(motion_task,
                                                 "motion_task",
                                                 MOTION_TASK_STACK_BYTES,
                                                 NULL,
                                                 MOTION_TASK_PRIORITY,
                                                 &s_motion_task,
                                                 1);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "motion task allocation failed");

    s_initialized = true;
    ESP_RETURN_ON_ERROR(motion_enable_all(false), TAG, "initial disable failed");
    ESP_LOGW(TAG, "temporary I2SO software step generator is enabled for bring-up");
    return ESP_OK;
}

esp_err_t motion_enable_all(bool enable)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    for (motion_axis_t axis = MOTION_AXIS_CONVEYOR; axis < MOTION_AXIS_COUNT; ++axis) {
        ESP_RETURN_ON_ERROR(write_axis_enable(axis, enable), TAG, "axis enable failed");
        ESP_RETURN_ON_ERROR(write_axis_step(axis, false), TAG, "axis step idle failed");
    }

    portENTER_CRITICAL(&s_motion_lock);
    s_enabled = enable;
    portEXIT_CRITICAL(&s_motion_lock);

    ESP_LOGI(TAG, "motion enable all = %d", enable);
    return ESP_OK;
}

esp_err_t motion_stop_all(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_motion_lock);
    s_stop_requested = true;
    portEXIT_CRITICAL(&s_motion_lock);

    for (motion_axis_t axis = MOTION_AXIS_CONVEYOR; axis < MOTION_AXIS_COUNT; ++axis) {
        (void)write_axis_step(axis, false);
        (void)write_axis_enable(axis, false);
    }

    portENTER_CRITICAL(&s_motion_lock);
    s_enabled = false;
    portEXIT_CRITICAL(&s_motion_lock);

    ESP_LOGW(TAG, "motion stop all");
    return ESP_OK;
}

esp_err_t motion_home_axis(motion_axis_t axis, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (!axis_valid(axis)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "home axis %d is not implemented yet", axis);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t motion_move_rel_mm(motion_axis_t axis, float distance_mm, float speed_mm_s)
{
    int32_t delta_steps = 0;
    ESP_RETURN_ON_ERROR(mm_to_steps(axis, distance_mm, &delta_steps), TAG, "relative conversion failed");
    return queue_move_steps(axis, delta_steps, speed_mm_s);
}

esp_err_t motion_move_abs_mm(motion_axis_t axis, float position_mm, float speed_mm_s)
{
    int32_t target_steps = 0;
    ESP_RETURN_ON_ERROR(mm_to_steps(axis, position_mm, &target_steps), TAG, "absolute conversion failed");

    portENTER_CRITICAL(&s_motion_lock);
    const int32_t current_steps = axis_valid(axis) ? s_position_steps[axis] : 0;
    portEXIT_CRITICAL(&s_motion_lock);

    return queue_move_steps(axis, target_steps - current_steps, speed_mm_s);
}

bool motion_is_busy(void)
{
    portENTER_CRITICAL(&s_motion_lock);
    const bool busy = s_busy;
    portEXIT_CRITICAL(&s_motion_lock);
    return busy;
}

esp_err_t motion_get_status(motion_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_motion_lock);
    status->initialized = s_initialized;
    status->enabled = s_enabled;
    status->busy = s_busy;
    status->active_axis = s_active_axis;
    for (int i = 0; i < MOTION_AXIS_COUNT; ++i) {
        status->position_steps[i] = s_position_steps[i];
    }
    portEXIT_CRITICAL(&s_motion_lock);
    return ESP_OK;
}
