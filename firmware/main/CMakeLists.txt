idf_component_register(SRCS "main.c" "nus.c" "frame.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver bt nvs_flash esp_timer)
