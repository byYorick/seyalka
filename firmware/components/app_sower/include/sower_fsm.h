#pragma once

#include "esp_err.h"
#include "recipe.h"
#include "sower_events.h"

typedef enum {
    SOWER_STATE_BOOT = 0,
    SOWER_STATE_SAFE_OUTPUTS_OFF,
    SOWER_STATE_LOAD_CONFIG,
    SOWER_STATE_INIT_BOARD,
    SOWER_STATE_HOME_TOOLS,
    SOWER_STATE_IDLE,
    SOWER_STATE_WAIT_CASSETTE,
    SOWER_STATE_RUN_CONVEYOR_FEED,
    SOWER_STATE_DETECT_CASSETTE_EDGE,
    SOWER_STATE_STOP_CONVEYOR,
    SOWER_STATE_INDEX_TO_FIRST_ROW_CENTER,
    SOWER_STATE_MAKE_HOLES,
    SOWER_STATE_PICK_SEEDS,
    SOWER_STATE_VERIFY_PICKUP,
    SOWER_STATE_TRANSFER_TO_DROP,
    SOWER_STATE_DROP_SEEDS,
    SOWER_STATE_BLOW_NEEDLES,
    SOWER_STATE_ADVANCE_TO_NEXT_ROW,
    SOWER_STATE_EJECT_CASSETTE,
    SOWER_STATE_FAULT,
    SOWER_STATE_ABORTED,
} sower_state_t;

typedef struct {
    sower_state_t state;
    uint16_t current_row;
    recipe_t recipe;
} sower_fsm_t;

esp_err_t sower_fsm_init(sower_fsm_t *fsm, const recipe_t *recipe);
esp_err_t sower_fsm_handle_event(sower_fsm_t *fsm, sower_event_t event);
const char *sower_state_name(sower_state_t state);

