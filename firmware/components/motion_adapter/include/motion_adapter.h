#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    MOTION_AXIS_CONVEYOR = 0,
    MOTION_AXIS_DIBBLER,
    MOTION_AXIS_TRANSFER,
    MOTION_AXIS_COUNT,
} motion_axis_t;

typedef struct {
    float max_speed_mm_s;
    float accel_mm_s2;
    float steps_per_mm;
} motion_axis_cfg_t;

typedef struct {
    motion_axis_cfg_t conveyor;
    motion_axis_cfg_t dibbler;
    motion_axis_cfg_t transfer;
} motion_config_t;

typedef struct {
    bool initialized;
    bool enabled;
    bool busy;
    motion_axis_t active_axis;
    int32_t position_steps[MOTION_AXIS_COUNT];
} motion_status_t;

esp_err_t motion_init(const motion_config_t *config);
esp_err_t motion_enable_all(bool enable);
esp_err_t motion_stop_all(void);
esp_err_t motion_home_axis(motion_axis_t axis, uint32_t timeout_ms);
esp_err_t motion_move_rel_mm(motion_axis_t axis, float distance_mm, float speed_mm_s);
esp_err_t motion_move_abs_mm(motion_axis_t axis, float position_mm, float speed_mm_s);
bool motion_is_busy(void);
esp_err_t motion_get_status(motion_status_t *status);
