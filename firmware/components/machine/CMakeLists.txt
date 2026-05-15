idf_component_register(
    SRCS
        "cassette_indexer.c"
        "dibbler.c"
        "transfer_arm.c"
        "vacuum_system.c"
        "pneumatics.c"
        "vibro_feeder.c"
    INCLUDE_DIRS "include"
    REQUIRES board_tinybee motion_adapter safety
)

