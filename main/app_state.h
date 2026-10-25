#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#define PROVISIONING_QR_MAX_SIZE 45
#define SETTINGS_CHANGED_BIT BIT0

typedef struct
{
    char main_desc[25];
    char icon[8];
    float temp;
    int humidity;
    float wind_speed;
    bool is_night;
    bool is_valid;
} weather_data_t;

typedef struct
{
    char callsign[16];
    char aircraftCode[16];
    char model[64];
    char category[32];
    char originIata[8];
    char originName[64];
    char destIata[8];
    char destName[64];
    long altitude;
    long groundSpeed;
} flight_info_t;

#ifdef __cplusplus
extern "C" {
#endif

extern SemaphoreHandle_t data_mutex;
extern weather_data_t g_weather;
extern bool g_weather_online;
extern bool g_time_synced;
extern float g_env_temp;
extern float g_env_hum;
extern bool g_shtc3_ok;
extern flight_info_t g_flight;
extern bool g_flightActive;
extern uint64_t g_flightStartMs;
extern const uint64_t FLIGHT_DISPLAY_MS;
extern uint8_t g_brightness;
extern uint8_t g_applied_brightness;
extern EventGroupHandle_t settings_events;
extern bool g_provisioning_active;
extern bool g_provisioning_qr_ready;
extern uint8_t g_provisioning_qr[PROVISIONING_QR_MAX_SIZE][PROVISIONING_QR_MAX_SIZE];
extern uint8_t g_provisioning_qr_size;

void app_state_init(void);

#ifdef __cplusplus
}
#endif

#endif
