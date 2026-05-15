idf_component_register(
    SRCS "board_tinybee.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_adc
        esp_timer
        grblhal_i2s_out
)
