#include "recipe.h"

#include <string.h>

void recipe_load_defaults(recipe_t *recipe)
{
    if (!recipe) {
        return;
    }

    memset(recipe, 0, sizeof(*recipe));
    recipe->version = RECIPE_VERSION_CURRENT;
    recipe->geometry.rows = 10;
    recipe->geometry.needles = 8;
    recipe->geometry.row_pitch_mm = 20.0f;
    recipe->geometry.first_row_offset_from_sensor_mm = 50.0f;
    recipe->geometry.conveyor_belt_reduction = 8.0f;
    recipe->geometry.conveyor_steps_per_mm = 80.0f;

    recipe->motion.conveyor = (motion_axis_cfg_t){ .max_speed_mm_s = 80.0f, .accel_mm_s2 = 200.0f, .steps_per_mm = 80.0f };
    recipe->motion.dibbler = (motion_axis_cfg_t){ .max_speed_mm_s = 20.0f, .accel_mm_s2 = 100.0f, .steps_per_mm = 400.0f };
    recipe->motion.transfer = (motion_axis_cfg_t){ .max_speed_mm_s = 30.0f, .accel_mm_s2 = 100.0f, .steps_per_mm = 100.0f };

    recipe->cassette = (cassette_indexer_config_t){
        .first_row_offset_from_sensor_mm = recipe->geometry.first_row_offset_from_sensor_mm,
        .row_pitch_mm = recipe->geometry.row_pitch_mm,
        .feed_speed_mm_s = 30.0f,
        .index_speed_mm_s = 20.0f,
        .cassette_timeout_ms = 10000,
    };

    recipe->dibbler = (dibbler_config_t){
        .safe_position_mm = 0.0f,
        .work_position_mm = 10.0f,
        .move_speed_mm_s = 10.0f,
        .dwell_ms = 100,
        .home_timeout_ms = 5000,
    };

    recipe->transfer = (transfer_arm_config_t){
        .safe_position_mm = 0.0f,
        .pick_position_mm = 10.0f,
        .drop_position_mm = 90.0f,
        .blow_position_mm = 90.0f,
        .move_speed_mm_s = 20.0f,
        .home_timeout_ms = 5000,
    };

    recipe->vacuum = (vacuum_config_t){
        .pickup_threshold_adc = 2000,
        .hold_min_adc = 1800,
        .release_threshold_adc = 500,
        .pickup_timeout_ms = 1500,
        .retry_count = 3,
    };

    recipe->pneumatics = (pneumatics_config_t){
        .vacuum_valve_on_delay_ms = 100,
        .pressure_pulse_ms = 150,
        .valve_deadtime_ms = 50,
    };

    recipe->vibro = (vibro_feeder_config_t){
        .enabled = false,
        .pwm_duty_pick_permille = 500,
        .pre_pick_ms = 200,
        .post_pick_ms = 100,
    };

    recipe->pickup_retry_count = 3;
}

bool recipe_validate(const recipe_t *recipe)
{
    return recipe &&
           recipe->version == RECIPE_VERSION_CURRENT &&
           recipe->geometry.rows > 0 &&
           recipe->geometry.needles == 8 &&
           recipe->geometry.row_pitch_mm > 0.0f &&
           recipe->geometry.conveyor_steps_per_mm > 0.0f;
}

