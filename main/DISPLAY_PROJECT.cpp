#include "time.h"

#include "app_state.h"
#include "wifi.h"
#include "server.h"
#include "sensor.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "hub75.h"
#include "draw_functions.h"

static Hub75Driver *g_driver = nullptr;
static const char *TAG = "DISPLAY_PROJECT";
extern "C" void pset(UG_S16 x, UG_S16 y, UG_COLOR c)
{
    if (x < 0 || x > 63 || y < 0 || y > 63)
        return;
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    g_driver->set_pixel(x, y, r, g, b);
}

// ================================================================
// MAIN DISPLAY TASK
// ================================================================
void display_task(void *arg)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    uint8_t initial_brightness = g_brightness;
    xSemaphoreGive(data_mutex);

    Hub75Config config{};
    config.panel_width = 64;
    config.panel_height = 64;
    config.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
    config.shift_driver = Hub75ShiftDriver::FM6126A;
    config.layout_rows = 1;
    config.layout_cols = 1;
    config.layout = Hub75PanelLayout::HORIZONTAL;
    config.pins.r1 = 4;
    config.pins.g1 = 5;
    config.pins.b1 = 6;
    config.pins.r2 = 7;
    config.pins.g2 = 15;
    config.pins.b2 = 16;
    config.pins.a = 18;
    config.pins.b = 8;
    config.pins.c = 3;
    config.pins.d = 42;
    config.pins.e = 9;
    config.pins.lat = 40;
    config.pins.oe = 2;
    config.pins.clk = 41;
    config.output_clock_speed = Hub75ClockSpeed::HZ_8M;
    // Keep the DMA row scan fast enough for phone rolling shutters. This
    // affects electrical scanning only; the QR framebuffer remains static.
    config.min_refresh_rate = 120;

    // Enabling double buffering to completely eliminate flicker
    config.double_buffer = true;
    config.brightness = initial_brightness;

    Hub75Driver driver(config);
    g_driver = &driver;
    if (!driver.begin())
    {
        ESP_LOGE(TAG, "HUB75 init failed");
        vTaskDelete(NULL);
    }
    g_applied_brightness = initial_brightness;

    UG_GUI gui;
    UG_Init(&gui, pset, 64, 64);

    while (1)
    {
        uint64_t nowMs = esp_timer_get_time() / 1000;

        xSemaphoreTake(data_mutex, portMAX_DELAY);
        uint8_t requested_brightness = g_brightness;
        xSemaphoreGive(data_mutex);
        if (requested_brightness != g_applied_brightness)
        {
            driver.set_brightness(requested_brightness);
            g_applied_brightness = requested_brightness;
        }

        xSemaphoreTake(data_mutex, portMAX_DELAY);
        bool provisioning_active = g_provisioning_active;
        bool wifi_startup_active = g_wifi_startup_active;
        if (g_flightActive && (nowMs - g_flightStartMs >= FLIGHT_DISPLAY_MS))
            g_flightActive = false;
        bool flight_active = g_flightActive;
        uint64_t flight_elapsed_ms = flight_active ? nowMs - g_flightStartMs : 0;
        xSemaphoreGive(data_mutex);

        time_t now;
        struct tm ti;
        time(&now);
        localtime_r(&now, &ti);

        UG_FillScreen(DISPLAY_BLACK);
        if (provisioning_active)
        {
            drawProvisioningQr();
        }
        else if (wifi_startup_active)
        {
            drawWifiStartup();
        }
        else
        {
            drawClockZone(&ti);

            if (flight_active)
                drawFlightZone(flight_elapsed_ms);
            else
            {
                drawEnvironmentZone();
                drawWeatherZone();
            }

            drawSeparators(ti.tm_sec, !flight_active);
            drawBorder(true);
        }
        // Swap back-buffer to screen
        g_driver->flip_buffer();

        if (provisioning_active)
        {
            // The QR frame has now been flipped once. Do not redraw or flip
            // another buffer while provisioning remains active.
            while (true)
            {
                xSemaphoreTake(data_mutex, portMAX_DELAY);
                bool still_provisioning = g_provisioning_active;
                xSemaphoreGive(data_mutex);
                if (!still_provisioning)
                    break;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(33)); // ~30fps
        }
    }
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    app_state_init();
    settings_init();
    xTaskCreate(display_task, "display_task", 16384, NULL, 5, NULL);
    initialize_wifi();
    setup_time();
    start_webserver();

    xTaskCreate(http_get_weather_task, "weather_task", 4096, NULL, 5, NULL);
    xTaskCreate(local_temp_humdity_task, "shtc3_task", 4096, NULL, 5, NULL);
}
