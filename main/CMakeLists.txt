idf_component_register(SRCS "DISPLAY_PROJECT.cpp" "ugui.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_wifi nvs_flash esp_http_client mbedtls lwip esp_http_server)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=parentheses" "-Wno-error=missing-field-initializers" "-Wno-error=unused-variable")