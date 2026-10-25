#include "wifi.h"
#include "qrcode.h"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

static void provisioning_qr_callback(esp_qrcode_handle_t qrcode)
{
    int size = esp_qrcode_get_size(qrcode);
    if (size <= 0 || size > PROVISIONING_QR_MAX_SIZE)
    {
        ESP_LOGE("wifi", "Provisioning QR code is too large for the display: %d", size);
        return;
    }

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    g_provisioning_qr_size = (uint8_t)size;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            g_provisioning_qr[y][x] = esp_qrcode_get_module(qrcode, x, y) ? 1 : 0;
    g_provisioning_qr_ready = true;
    g_provisioning_active = true;
    xSemaphoreGive(data_mutex);
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
        }
        else
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void start_station(void)
{
    wifi_config_t wifi_config = {0};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void initialize_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    wifi_prov_mgr_config_t manager_config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(manager_config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    if (!provisioned)
    {
        char service_name[20];
        snprintf(service_name, sizeof(service_name), "MatrixCast-%02X%02X",
                 (unsigned)esp_random() & 0xFF, (unsigned)(esp_random() >> 8) & 0xFF);
        ESP_LOGI("wifi", "No Wi-Fi credentials. Connect to AP %s and provision the device.", service_name);
        static const char *const provisioning_pop = "MatrixCast";
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"softap\"}",
                 service_name, provisioning_pop);
        esp_qrcode_config_t qr_config = ESP_QRCODE_CONFIG_DEFAULT();
        qr_config.display_func = provisioning_qr_callback;
        qr_config.max_qrcode_version = 5;
        ESP_ERROR_CHECK(esp_qrcode_generate(&qr_config, payload));
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_1, (const void *)provisioning_pop, service_name, NULL));
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        g_provisioning_active = false;
        xSemaphoreGive(data_mutex);
        wifi_prov_mgr_stop_provisioning();
        wifi_prov_mgr_deinit();
    }
    else
    {
        wifi_prov_mgr_deinit();
        start_station();
    }

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}


void setup_time(void)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    char timezone[SETTINGS_TIMEZONE_MAX];
    snprintf(timezone, sizeof(timezone), "%s", g_settings.timezone);
    xSemaphoreGive(data_mutex);
    setenv("TZ", timezone, 1);
    tzset();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2026 - 1900))
    {
        ESP_LOGI("wifi", "Setting time from Internet...");
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&config);
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) != ESP_OK)
            ESP_LOGE("wifi", "Failed to sync time via SNTP timeout(5s)");
        else
            g_time_synced = true;
    }
    else
        g_time_synced = true;
}