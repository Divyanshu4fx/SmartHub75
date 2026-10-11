idf_component_register(SRCS "DISPLAY_PROJECT.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_wifi nvs_flash esp_http_client mbedtls lwip)
