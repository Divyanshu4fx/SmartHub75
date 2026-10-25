#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

#define SETTINGS_CITY_MAX 64
#define SETTINGS_API_KEY_MAX 96
#define SETTINGS_TIMEZONE_MAX 40

typedef struct
{
    char city[SETTINGS_CITY_MAX];
    char api_key[SETTINGS_API_KEY_MAX];
    char timezone[SETTINGS_TIMEZONE_MAX];
} app_settings_t;

#ifdef __cplusplus
extern "C" {
#endif

extern app_settings_t g_settings;

void settings_init(void);
bool settings_update(const app_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif
