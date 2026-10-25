idf_component_register(SRCS "DISPLAY_PROJECT.cpp" "app_state.c" "settings.c" "ui/ugui.c" "ui/draw_functions.c" "wifi/wifi.c" "sensor/sensor.c" "local_server/server.c"
                    INCLUDE_DIRS "." "local_server" "ui" "sensor" "wifi"
                    REQUIRES esp_driver_gpio esp_wifi nvs_flash esp_http_client mbedtls lwip esp_http_server wifi_provisioning qrcode mdns)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=parentheses" "-Wno-error=missing-field-initializers" "-Wno-error=unused-variable")