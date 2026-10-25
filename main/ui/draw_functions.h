#ifndef DRAW_FUNCTIONS_H
#define DRAW_FUNCTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "app_state.h"
#include "display_config.h"
#include "ugui.h"

#ifdef __cplusplus
extern "C" {
#endif

void pset(UG_S16 x, UG_S16 y, UG_COLOR c);
void drawClockZone(const struct tm *ti);
void drawEnvironmentZone(void);
void drawWeatherZone(void);
void drawFlightZone(uint64_t elapsed);
void drawSeparators(int sec, bool showSep2);
void drawBorder(bool animate);
void drawProvisioningQr(void);

#ifdef __cplusplus
}
#endif

#endif