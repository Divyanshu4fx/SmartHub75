#ifndef SERVER_H
#define SERVER_H

#include "app_state.h"
#include "settings.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "time.h"

#define OWM_UNITS "metric"

#ifdef __cplusplus
extern "C" {
#endif

void http_get_weather_task(void *arg);
httpd_handle_t start_webserver(void);

#ifdef __cplusplus
}
#endif

#endif