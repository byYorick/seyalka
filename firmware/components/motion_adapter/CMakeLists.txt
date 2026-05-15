idf_component_register(
    SRCS "motion_adapter.c"
    INCLUDE_DIRS "include"
    REQUIRES board_tinybee grblhal_i2s_out
)
