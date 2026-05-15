#pragma once

#include "esp_err.h"
#include "recipe.h"

esp_err_t config_store_init(void);
esp_err_t config_store_load_recipe(recipe_t *recipe);
esp_err_t config_store_save_recipe(const recipe_t *recipe);

