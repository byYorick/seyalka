idf_component_register(
    SRCS "grblhal_i2s_out.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_hw_support
        freertos
        heap
        soc
)
