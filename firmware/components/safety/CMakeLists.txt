idf_component_register(
    SRCS "safety.c"
    INCLUDE_DIRS "include"
    REQUIRES board_tinybee motion_adapter esp_timer
)
