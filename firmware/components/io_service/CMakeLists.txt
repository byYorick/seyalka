idf_component_register(
    SRCS "io_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board_tinybee
        freertos
)
