#include "config_store.h"

#include "nvs_flash.h"

esp_err_t config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t config_store_load_recipe(recipe_t *recipe)
{
    if (!recipe) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Persistent recipe loading will be added after NVS schema is frozen. */
    recipe_load_defaults(recipe);
    return ESP_OK;
}

esp_err_t config_store_save_recipe(const recipe_t *recipe)
{
    if (!recipe || !recipe_validate(recipe)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Persistent writes are intentionally explicit and not implemented in skeleton. */
    return ESP_ERR_NOT_SUPPORTED;
}

