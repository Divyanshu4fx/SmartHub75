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
#include "driver/gpio.h"
#include "cJSON.h"
#include "time.h"

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

void setup_time(void)
{

    setenv("TZ","IST-5:30",1);
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
            ESP_LOGI(TAG, "Response : %s", response_buf);
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

while(1)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
    // }
}

void app_main(void)
{
    // ESP_LOGI("DISPLAY_PROJECT", "Hello, World!");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    initialize_wifi();
    ESP_LOGI(TAG, "Starting Weather Task");
    setup_time();
    xTaskCreate(http_get_weather_task, "http_get_weather_task", 4096, NULL, 5, NULL);
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
