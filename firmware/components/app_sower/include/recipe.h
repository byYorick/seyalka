#pragma once

#include <stdint.h>
#include "cassette_indexer.h"
#include "dibbler.h"
#include "motion_adapter.h"
#include "pneumatics.h"
#include "transfer_arm.h"
#include "vacuum_system.h"
#include "vibro_feeder.h"

#define RECIPE_VERSION_CURRENT 1U

typedef struct {
    uint16_t rows;
    uint8_t needles;
    float row_pitch_mm;
    float first_row_offset_from_sensor_mm;
    float conveyor_belt_reduction;
    float conveyor_steps_per_mm;
} recipe_geometry_t;

typedef struct {
    recipe_geometry_t geometry;
    motion_config_t motion;
    cassette_indexer_config_t cassette;
    dibbler_config_t dibbler;
    transfer_arm_config_t transfer;
    vacuum_config_t vacuum;
    pneumatics_config_t pneumatics;
    vibro_feeder_config_t vibro;
    uint8_t pickup_retry_count;
    uint32_t version;
    uint32_t crc32;
} recipe_t;

void recipe_load_defaults(recipe_t *recipe);
bool recipe_validate(const recipe_t *recipe);

