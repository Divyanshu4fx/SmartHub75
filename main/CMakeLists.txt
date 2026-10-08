idf_component_register(SRCS "DISPLAY_PROJECT.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_wifi nvs_flash)
