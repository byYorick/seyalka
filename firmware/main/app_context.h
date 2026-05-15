#pragma once

#include "recipe.h"
#include "sower_fsm.h"

typedef struct {
    recipe_t recipe;
    sower_fsm_t fsm;
} app_context_t;

