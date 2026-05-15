#include "sower_fsm.h"

#include <string.h>
#include "cassette_indexer.h"
#include "dibbler.h"
#include "esp_check.h"
#include "esp_log.h"
#include "fault_manager.h"
#include "motion_adapter.h"
#include "pneumatics.h"
#include "safety.h"
#include "transfer_arm.h"
#include "vacuum_system.h"
#include "vibro_feeder.h"

static const char *TAG = "sower_fsm";

static esp_err_t transition_to(sower_fsm_t *fsm, sower_state_t next)
{
    ESP_LOGI(TAG, "%s -> %s", sower_state_name(fsm->state), sower_state_name(next));
    fsm->state = next;
    return ESP_OK;
}

esp_err_t sower_fsm_init(sower_fsm_t *fsm, const recipe_t *recipe)
{
    if (!fsm || !recipe || !recipe_validate(recipe)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(fsm, 0, sizeof(*fsm));
    fsm->state = SOWER_STATE_BOOT;
    fsm->recipe = *recipe;

    ESP_RETURN_ON_ERROR(motion_init(&recipe->motion), TAG, "motion init failed");
    ESP_RETURN_ON_ERROR(cassette_indexer_init(&recipe->cassette), TAG, "cassette init failed");
    ESP_RETURN_ON_ERROR(dibbler_init(&recipe->dibbler), TAG, "dibbler init failed");
    ESP_RETURN_ON_ERROR(transfer_arm_init(&recipe->transfer), TAG, "transfer init failed");
    ESP_RETURN_ON_ERROR(vacuum_system_init(&recipe->vacuum), TAG, "vacuum init failed");
    ESP_RETURN_ON_ERROR(pneumatics_init(&recipe->pneumatics), TAG, "pneumatics init failed");
    ESP_RETURN_ON_ERROR(vibro_feeder_init(&recipe->vibro), TAG, "vibro init failed");

    return transition_to(fsm, SOWER_STATE_IDLE);
}

esp_err_t sower_fsm_handle_event(sower_fsm_t *fsm, sower_event_t event)
{
    if (!fsm) {
        return ESP_ERR_INVALID_ARG;
    }

    if (event == SOWER_EVENT_ESTOP) {
        fault_manager_raise(FAULT_ESTOP, FAULT_SEVERITY_CRITICAL);
        (void)safety_enter_safe_state();
        return transition_to(fsm, SOWER_STATE_ABORTED);
    }

    switch (fsm->state) {
    case SOWER_STATE_IDLE:
        if (event == SOWER_EVENT_START_STOP && safety_can_start_cycle()) {
            fsm->current_row = 0;
            return transition_to(fsm, SOWER_STATE_WAIT_CASSETTE);
        }
        break;

    case SOWER_STATE_WAIT_CASSETTE:
        ESP_RETURN_ON_ERROR(cassette_wait_and_capture(), TAG, "cassette feed failed");
        return transition_to(fsm, SOWER_STATE_RUN_CONVEYOR_FEED);

    case SOWER_STATE_DETECT_CASSETTE_EDGE:
        ESP_RETURN_ON_ERROR(motion_stop_all(), TAG, "conveyor stop failed");
        return transition_to(fsm, SOWER_STATE_INDEX_TO_FIRST_ROW_CENTER);

    case SOWER_STATE_INDEX_TO_FIRST_ROW_CENTER:
        ESP_RETURN_ON_ERROR(cassette_move_to_first_row(), TAG, "first row index failed");
        return transition_to(fsm, SOWER_STATE_MAKE_HOLES);

    default:
        break;
    }

    return ESP_OK;
}

const char *sower_state_name(sower_state_t state)
{
    switch (state) {
    case SOWER_STATE_BOOT: return "BOOT";
    case SOWER_STATE_SAFE_OUTPUTS_OFF: return "SAFE_OUTPUTS_OFF";
    case SOWER_STATE_LOAD_CONFIG: return "LOAD_CONFIG";
    case SOWER_STATE_INIT_BOARD: return "INIT_BOARD";
    case SOWER_STATE_HOME_TOOLS: return "HOME_TOOLS";
    case SOWER_STATE_IDLE: return "IDLE";
    case SOWER_STATE_WAIT_CASSETTE: return "WAIT_CASSETTE";
    case SOWER_STATE_RUN_CONVEYOR_FEED: return "RUN_CONVEYOR_FEED";
    case SOWER_STATE_DETECT_CASSETTE_EDGE: return "DETECT_CASSETTE_EDGE";
    case SOWER_STATE_STOP_CONVEYOR: return "STOP_CONVEYOR";
    case SOWER_STATE_INDEX_TO_FIRST_ROW_CENTER: return "INDEX_TO_FIRST_ROW_CENTER";
    case SOWER_STATE_MAKE_HOLES: return "MAKE_HOLES";
    case SOWER_STATE_PICK_SEEDS: return "PICK_SEEDS";
    case SOWER_STATE_VERIFY_PICKUP: return "VERIFY_PICKUP";
    case SOWER_STATE_TRANSFER_TO_DROP: return "TRANSFER_TO_DROP";
    case SOWER_STATE_DROP_SEEDS: return "DROP_SEEDS";
    case SOWER_STATE_BLOW_NEEDLES: return "BLOW_NEEDLES";
    case SOWER_STATE_ADVANCE_TO_NEXT_ROW: return "ADVANCE_TO_NEXT_ROW";
    case SOWER_STATE_EJECT_CASSETTE: return "EJECT_CASSETTE";
    case SOWER_STATE_FAULT: return "FAULT";
    case SOWER_STATE_ABORTED: return "ABORTED";
    default: return "UNKNOWN";
    }
}
