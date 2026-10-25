#ifndef WIFI_H
#define WIFI_H

#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "app_state.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define WIFI_MAX_RETRY 5

#ifdef __cplusplus
extern "C" {
#endif

void initialize_wifi(void);
void setup_time(void);

#ifdef __cplusplus
}
#endif

#endif