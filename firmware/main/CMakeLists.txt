idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_sower
        board_tinybee
        io_service
        motion_adapter
        safety
        storage
        ui_display
        ui_web
)
