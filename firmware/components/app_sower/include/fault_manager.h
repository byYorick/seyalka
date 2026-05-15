#pragma once

#include <stdint.h>

typedef enum {
    FAULT_NONE = 0,
    FAULT_ESTOP,
    FAULT_CONFIG_INVALID,
    FAULT_HOME_DIBBLER_FAILED,
    FAULT_HOME_TRANSFER_FAILED,
    FAULT_CASSETTE_TIMEOUT,
    FAULT_CASSETTE_SENSOR_INVALID,
    FAULT_PICKUP_VACUUM_TIMEOUT,
    FAULT_VACUUM_LOST,
    FAULT_VACUUM_RELEASE_FAILED,
    FAULT_ADC_RANGE,
    FAULT_MOTION_TIMEOUT,
} fault_code_t;

typedef enum {
    FAULT_SEVERITY_INFO = 0,
    FAULT_SEVERITY_WARN,
    FAULT_SEVERITY_RECOVERABLE,
    FAULT_SEVERITY_CRITICAL,
} fault_severity_t;

typedef struct {
    fault_code_t code;
    fault_severity_t severity;
    uint16_t row;
    uint16_t vacuum_adc;
} fault_record_t;

void fault_manager_clear(void);
void fault_manager_raise(fault_code_t code, fault_severity_t severity);
fault_record_t fault_manager_get_last(void);

