#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_timer.h"
#include "app_state.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_NAMESPACE "smarthub75"

app_settings_t g_settings;
EventGroupHandle_t settings_events;

static const app_settings_t DEFAULT_SETTINGS = {
    .city = "Navi Mumbai,IN",
    .api_key = "",
    .timezone = "IST-5:30",
};

static bool valid_string(const char *value, size_t capacity)
{
    return value != NULL && strnlen(value, capacity) < capacity;
}

void settings_init(void)
{
    g_settings = DEFAULT_SETTINGS;
    settings_events = xEventGroupCreate();
    configASSERT(settings_events != NULL);

    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        return;

    nvs_get_str(handle, "city", g_settings.city, &(size_t){sizeof(g_settings.city)});
    nvs_get_str(handle, "api_key", g_settings.api_key, &(size_t){sizeof(g_settings.api_key)});
    nvs_get_str(handle, "timezone", g_settings.timezone, &(size_t){sizeof(g_settings.timezone)});
    nvs_close(handle);
}

bool settings_update(const app_settings_t *settings)
{
    if (!settings || !valid_string(settings->city, sizeof(settings->city)) ||
        !valid_string(settings->api_key, sizeof(settings->api_key)) ||
        !valid_string(settings->timezone, sizeof(settings->timezone)) ||
        settings->city[0] == '\0' || settings->api_key[0] == '\0' ||
        settings->timezone[0] == '\0')
        return false;

    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return false;

    esp_err_t err = nvs_set_str(handle, "city", settings->city);
    if (err == ESP_OK)
        err = nvs_set_str(handle, "api_key", settings->api_key);
    if (err == ESP_OK)
        err = nvs_set_str(handle, "timezone", settings->timezone);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK)
        return false;

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    g_settings = *settings;
    xSemaphoreGive(data_mutex);
    xEventGroupSetBits(settings_events, SETTINGS_CHANGED_BIT);

    setenv("TZ", g_settings.timezone, 1);
    tzset();
    return true;
}
