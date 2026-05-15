#include "fault_manager.h"

static fault_record_t s_last_fault = {
    .code = FAULT_NONE,
    .severity = FAULT_SEVERITY_INFO,
};

void fault_manager_clear(void)
{
    s_last_fault = (fault_record_t){
        .code = FAULT_NONE,
        .severity = FAULT_SEVERITY_INFO,
    };
}

void fault_manager_raise(fault_code_t code, fault_severity_t severity)
{
    s_last_fault.code = code;
    s_last_fault.severity = severity;
}

fault_record_t fault_manager_get_last(void)
{
    return s_last_fault;
}

