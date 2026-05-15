idf_component_register(
    SRCS
        "recipe.c"
        "fault_manager.c"
        "sower_fsm.c"
        "row_process.c"
    INCLUDE_DIRS "include"
    REQUIRES machine motion_adapter safety
)

