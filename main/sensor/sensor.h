#ifndef SENSOR_H
#define SENSOR_H

#include "app_state.h"
#include "shtc3.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

void local_temp_humdity_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif