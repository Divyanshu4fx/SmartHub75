#include "app_state.h"

SemaphoreHandle_t data_mutex;
weather_data_t g_weather = {.is_valid = false};
bool g_weather_online = false;
bool g_time_synced = false;
float g_env_temp = 0.0f;
float g_env_hum = 0.0f;
bool g_shtc3_ok = false;
flight_info_t g_flight;
bool g_flightActive = false;
uint64_t g_flightStartMs = 0;
const uint64_t FLIGHT_DISPLAY_MS = 30000;
uint8_t g_brightness = 75;
uint8_t g_applied_brightness = 0;
bool g_provisioning_active = false;
bool g_provisioning_qr_ready = false;
bool g_wifi_startup_active = false;
uint8_t g_provisioning_qr[PROVISIONING_QR_MAX_SIZE][PROVISIONING_QR_MAX_SIZE];
uint8_t g_provisioning_qr_size = 0;

void app_state_init(void)
{
    data_mutex = xSemaphoreCreateMutex();
    configASSERT(data_mutex != NULL);
}
