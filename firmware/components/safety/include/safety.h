#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    SAFETY_STATE_BOOT = 0,
    SAFETY_STATE_READY,
    SAFETY_STATE_ESTOP,
    SAFETY_STATE_FAULT,
} safety_state_t;

typedef enum {
    SAFETY_FAULT_NONE = 0,
    SAFETY_FAULT_REQUESTED,
    SAFETY_FAULT_OUTPUT_OFF_FAILED,
    SAFETY_FAULT_MOTION_STOP_FAILED,
    SAFETY_FAULT_VACUUM_PRESSURE_CONFLICT,
    SAFETY_FAULT_CONTROL_WATCHDOG_TIMEOUT,
} safety_fault_t;

typedef struct {
    uint32_t check_period_ms;
    uint32_t task_stack_bytes;
    uint8_t task_priority;
    uint32_t control_watchdog_timeout_ms;
} safety_config_t;

typedef struct {
    safety_state_t state;
    safety_fault_t latched_fault;
    uint32_t last_heartbeat_ms;
    uint32_t control_watchdog_timeout_ms;
} safety_status_t;

esp_err_t safety_get_default_config(safety_config_t *config);
esp_err_t safety_init(void);
esp_err_t safety_init_with_config(const safety_config_t *config);
esp_err_t safety_enter_safe_state(void);
esp_err_t safety_check_periodic(void);
esp_err_t safety_ack_fault(void);
esp_err_t safety_control_heartbeat(void);
esp_err_t safety_configure_watchdog(uint32_t timeout_ms);
bool safety_can_start_cycle(void);
safety_state_t safety_get_state(void);
safety_fault_t safety_get_latched_fault(void);
esp_err_t safety_get_status(safety_status_t *status);
