/*
  ================================================================
  WAVESHARE ESP32-S3 RGB MATRIX
  64x64 DASHBOARD  -  "BIG BORDER" EDITION + FLIGHT OVERLAY
  ================================================================

  Normal mode (3 layers):
    1. Clock (12h IST) + AM/PM badge + date + seconds bar
    2. SHTC3 indoor temperature + humidity
    3. Online weather

  Flight mode (triggered by HTTP POST, lasts 30 s):
    Layers 2 + 3 are replaced by one combined flight panel:

      | plane icon | CALLSIGN
      |            | Aircraft model (scrolls right -> left)
      | ORG: origin name (scrolls)
      | DST: destination name (scrolls)

    Icon is chosen from "aircraft_category":
      Helicopter / Piston / Turboprop / Turbofan / anything else = "?"

  Web server:
    POST http://<board-ip>/flight   body = JSON (see below)
    POST http://<board-ip>/clear    ends flight mode early
    GET  http://<board-ip>/         status text

    {
      "callsign": "AIC101",
      "aircraft_code": "B77W",
      "aircraft_model": "Boeing 777-337(ER)",
      "aircraft_category": "Turbofan",
      "origin_iata": "BOM",
      "origin_icao": "VABB",
      "origin_name": "Mumbai Chhatrapati Shivaji Maharaj International Airport",
      "destination_iata": "JFK",
      "destination_icao": "KJFK",
      "destination_name": "John F. Kennedy International Airport",
      "altitude": 32000,
      "ground_speed": 485
    }

    curl example:
      curl -X POST http://<board-ip>/flight \
           -H "Content-Type: application/json" -d @flight.json

  IMPORTANT 64x64 PANEL CONFIGURATION:
    E = GPIO9, CLK/LAT/OE = default library mapping

  I2C:
    SDA = GPIO47
    SCL = GPIO48

  LAYOUT MAP (y rows, inner area is x 3..60 / y 3..60):
     0- 2  border
     5-11  time (thick digits)
    13-21  AM/PM badge + date
    24-25  separator 1  (seconds bar)
    --- normal mode ---            --- flight mode ---
    28-34  indoor temperature      26-44  icon (x 3..23) | callsign y27, model y37
    37-43  indoor humidity         45     thin divider
    46-47  separator 2             46-52  origin line
    51-57  weather                 54-60  destination line
    61-63  border
*/

#include <string.h>
#include <stdlib.h>
#include "time.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "cJSON.h"
#include "shtc3.h"
#include "hub75.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"

// ================================================================
// WIFI / WEATHER SETTINGS
// ================================================================
#define WIFI_SSID "Airtel_divy_6706"
#define WIFI_PASSWORD "Air@33932"
#define WIFI_MAX_RETRY 5

#define OWM_API_KEY "4c5900b3563744c20a321add8ef2ac8c"
#define OWM_CITY "Navi%20Mumbai,IN"
#define OWM_UNITS "metric"

#define BUFF_SIZE 512
static char response_buf[BUFF_SIZE];
static int response_len = 0;

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "DISPLAY_PROJECT";

static int s_retry_num = 0;

typedef struct
{
    char main[25];
    float temp;       // °C
    float feels_like; // °C
    int pressure;     // hpa
    int humidity;     // %
    float wind_speed; // m/s
    int wind_dirc;    // ° deg
} weather_data_t;

void setup_time(void)
{

    setenv("TZ", "IST-5:30", 1);
    tzset();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2026 - 1900))
    {
        ESP_LOGI(TAG, "Setting time from Internet...");
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&config);
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) != ESP_OK)
            ESP_LOGE(TAG, "Failed to sync time via SNTP timeout(5s)");
        else
        {
            time(&now);
            localtime_r(&now, &timeinfo);
        }
    }
    char str_time_buf[64];
    strftime(str_time_buf, sizeof(str_time_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current date/time is : %s", str_time_buf);
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < WIFI_MAX_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retrying to connect to Wifi...");
        }
        else
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        ESP_LOGW(TAG, "Connection to wifi is failing...");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP : " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void initialize_wifi(void)
{
    ESP_LOGI(TAG, "Starting WIFI Initialization...");
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            // .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            // .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            // .sae_h2e_identifier = "",
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
        ESP_LOGI(TAG, "Wifi Connected Succesfully");
    else if (bits & WIFI_FAIL_BIT)
        ESP_LOGE(TAG, "Wifi Connection Failed");
    else
        ESP_LOGE(TAG, "Unexpected Event");
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA)
    {
        int copy = event->data_len;
        if (response_len + copy >= BUFF_SIZE)
            copy = BUFF_SIZE - response_len - 1;
        memcpy(response_buf + response_len, event->data, copy);
        response_len += copy;
        response_buf[response_len] = '\0';
    }
    return ESP_OK;
}

void http_get_weather_task(void *arg)
{
    char url[256];

    snprintf(url, sizeof(url), "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s", OWM_CITY, OWM_API_KEY, OWM_UNITS);
    ESP_LOGI(TAG, "URL : %s", url);
    // "http://api.openweathermap.org/data/2.5/weather?q=" + OWM_CITY + "&appid=" + OWM_API_KEY + "&units=" + OWM_UNITS;

    response_len = 0;
    memset(&response_buf, 0, BUFF_SIZE);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        if (status == 200)
        {
            // ESP_LOGI(TAG, "Response : %s", response_buf);
            cJSON *json = cJSON_Parse(response_buf);
            if (json != NULL)
            {
                ESP_LOGI(TAG, "JSON Parsing Successful");
                weather_data_t weather_data;

                cJSON *weather = cJSON_GetObjectItem(json, "weather");
                cJSON *weather_item = NULL;
                cJSON_ArrayForEach(weather_item, weather)
                {
                    snprintf(weather_data.main, sizeof(weather_data.main), "%s", cJSON_GetObjectItem(weather_item, "main")->valuestring);
                }
                cJSON *main = cJSON_GetObjectItem(json, "main");
                weather_data.temp = cJSON_GetObjectItem(main, "temp")->valuedouble;
                weather_data.feels_like = cJSON_GetObjectItem(main, "feels_like")->valuedouble;
                weather_data.pressure = cJSON_GetObjectItem(main, "pressure")->valueint;
                weather_data.humidity = cJSON_GetObjectItem(main, "humidity")->valueint;
                cJSON *wind = cJSON_GetObjectItem(json, "wind");
                weather_data.wind_speed = cJSON_GetObjectItem(wind, "speed")->valuedouble;
                weather_data.wind_dirc = cJSON_GetObjectItem(wind, "deg")->valueint;

                ESP_LOGI(TAG, "Weather Data : main=%s, temp=%.2f, feels_like=%.2f, pressure=%d, humidity=%d, wind_speed=%.2f, wind_dirc=%d",
                         weather_data.main,
                         weather_data.temp,
                         weather_data.feels_like,
                         weather_data.pressure,
                         weather_data.humidity,
                         weather_data.wind_speed,
                         weather_data.wind_dirc);
                cJSON_Delete(json);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to parse JSON response");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Status Code : %d", status);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error : %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // }
}

void local_temp_humdity_task(void *arg)
{
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_47,
        .scl_io_num = GPIO_NUM_48,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags =
            {
                .enable_internal_pullup = true}};

    i2c_master_bus_handle_t i2c_handle;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_handle));
    ESP_LOGI(TAG, "I2C bus initialized successfully");

    i2c_master_dev_handle_t shtc3_handle = shtc3_device_create(i2c_handle, SHTC3_I2C_ADDR, CONFIG_SHTC3_I2C_CLK_SPEED_HZ);

    float temperature = 0.0f;
    float humidity = 0;
    while (1)
    {
        shtc3_get_th(shtc3_handle, SHTC3_REG_T_CSE_NM, &temperature, &humidity);
        ESP_LOGI(TAG, "Local Temperature: %.2f°C, Humidity: %.2f%%", temperature, humidity);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// void app_main(void)
// {
//     // ESP_LOGI("DISPLAY_PROJECT", "Hello, World!");
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
//     {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);
//     initialize_wifi();
//     ESP_LOGI(TAG, "Starting Weather Task");
//     setup_time();
//     xTaskCreate(http_get_weather_task, "http_get_weather_task", 4096, NULL, 5, NULL);
//     xTaskCreate(local_temp_humdity_task, "local_temp_humdity_task", 4096, NULL, 5, NULL);
//     while (1)
//     {
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }

extern "C" void app_main(void)
{
    // Configure for your panel
    Hub75Config config{};

    // Single panel specifications
    config.panel_width = 64;
    config.panel_height = 64;
    config.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
    config.shift_driver = Hub75ShiftDriver::FM6126A;
    // config.output_clock_speed = Hub75ClockSpeed::HZ_8M;

    // Single panel layout
    config.layout_rows = 1;
    config.layout_cols = 1;
    config.layout = Hub75PanelLayout::HORIZONTAL;

    // Data pins - Upper half (R1/G1/B1)
    config.pins.r1 = 4;
    config.pins.g1 = 5;
    config.pins.b1 = 6;

    // Data pins - Lower half (R2/G2/B2)
    config.pins.r2 = 7;
    config.pins.g2 = 15;
    config.pins.b2 = 16;

    // Address lines (A, B, C, D, E)
    config.pins.a = 18;
    config.pins.b = 8;
    config.pins.c = 3;
    config.pins.d = 42;
    config.pins.e = 9; // Required for 64-row panels (1/32 scan)

    // Control signals
    config.pins.lat = 40; // Latch
    config.pins.oe = 2;   // Output Enable (active low)
    config.pins.clk = 41; // Clock

    // Performance settings
    config.output_clock_speed = Hub75ClockSpeed::HZ_8M; // 20 MHz
    // Bit depth: Configure via menuconfig (idf.py menuconfig → HUB75 → Panel Settings)
    config.min_refresh_rate = 60; // 60 Hz minimum

    // Features
    config.double_buffer = false;
    // Gamma correction: Configure via menuconfig (Kconfig HUB75_GAMMA_MODE)
    config.brightness = 5;

    // Create and start driver
    Hub75Driver driver(config);
    driver.begin(); // Starts continuous refresh

    const uint16_t width = driver.get_width();
    const uint16_t height = driver.get_height();

    // Colors for bars (R, G, B)
    const struct
    {
        uint8_t r, g, b;
    } colors[] = {
        {255, 0, 0},    // Red
        {0, 255, 0},    // Green
        {0, 0, 255},    // Blue
        {255, 255, 0},  // Yellow
        {0, 255, 255},  // Cyan
        {255, 0, 255},  // Magenta
        {255, 255, 255} // White
    };

    const uint16_t num_colors = sizeof(colors) / sizeof(colors[0]);
    const uint16_t bar_height = height / num_colors;

    for (uint16_t i = 0; i < num_colors; i++)
    {
        uint16_t y_start = i * bar_height;
        uint16_t y_end = (i + 1) * bar_height;
        if (i == num_colors - 1)
        {
            y_end = height; // Last bar fills remaining height
        }

        for (uint16_t y = y_start; y < y_end; y++)
        {
            for (uint16_t x = 0; x < width; x++)
            {
                driver.set_pixel(x, y, colors[i].r, colors[i].g, colors[i].b);
            }
        }
    }


    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG,"Looping...");
    }
}
