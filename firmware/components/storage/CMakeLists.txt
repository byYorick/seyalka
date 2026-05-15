idf_component_register(
    SRCS "config_store.c" "event_log.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash app_sower
)

