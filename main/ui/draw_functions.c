#include "draw_functions.h"
#include "icons.h"

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"

static const UG_COLOR COMET[5] = {
    DISPLAY_WHITE, 0x96F0FF, 0x3CC8FF, 0x148CFF, 0x0A64FF};
static const char *const MONTHS[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

int charPitch(bool thick)
{
    return thick ? 7 : 6;
}

int textWidth(const char *s, bool thick)
{
    int n = strlen(s);
    return n > 0 ? n * charPitch(thick) - 1 : 0;
}

// Exactly matches Adafruit GFX and the "thick" bold pseudo-effect
void drawGlyph(int x, int y, char c, UG_COLOR color, bool thick)
{
    if (c < 32 || c > 126)
        c = '?';
    const uint8_t *font_col = font5x7[c - 32];
    for (int i = 0; i < 5; i++)
    {
        uint8_t line = font_col[i];
        for (int j = 0; j < 8; j++, line >>= 1)
        {
            if (line & 1)
            {
                pset(x + i, y + j, color);
                if (thick)
                    pset(x + i + 1, y + j, color);
            }
        }
    }
}

void drawString(int x, int y, const char *s, UG_COLOR color, bool thick)
{
    int pitch = charPitch(thick);
    for (int i = 0; s[i]; i++)
    {
        drawGlyph(x + i * pitch, y, s[i], color, thick);
    }
}

void drawCentered(const char *s, int y, UG_COLOR color, bool thick)
{
    int x = IN_L + (IN_W - textWidth(s, thick)) / 2;
    drawString(x, y, s, color, thick);
}

UG_COLOR iconColor(char c)
{
    switch (c)
    {
    case 'Y':
        return DISPLAY_MY_YELLOW;
    case 'O':
        return DISPLAY_ORANGE;
    case 'W':
        return DISPLAY_WHITE;
    case 'G':
        return DISPLAY_MY_GRAY;
    case 'B':
        return DISPLAY_ICON_BLUE;
    case 'R':
        return DISPLAY_MY_RED;
    case 'S':
        return DISPLAY_SNOW;
    case 'M':
        return DISPLAY_MOON;
    case 'P':
        return DISPLAY_SNOW;
    case 'p':
        return DISPLAY_MY_GRAY;
    default:
        return DISPLAY_WHITE;
    }
}

void drawIconN(int x, int y, const char *const *rows, int height)
{
    for (int r = 0; r < height; r++)
    {
        const char *line = rows[r];
        for (int c = 0; line[c]; c++)
        {
            if (line[c] != '.')
                pset(x + c, y + r, iconColor(line[c]));
        }
    }
}

void drawIcon(int x, int y, const char *const *rows)
{
    drawIconN(x, y, rows, 7);
}

void drawTempValue(int x, int y, const char *num, UG_COLOR color, bool thick)
{
    drawString(x, y, num, color, thick);
    int rx = x + strlen(num) * charPitch(thick);
    UG_FillFrame(rx, y, rx + 2, y + 2, color); // 3x3 rect => x to x+2
    drawGlyph(rx + 4, y, 'C', color, thick);
}

UG_COLOR tempColor(float t)
{
    if (t < 18)
        return DISPLAY_LIGHT_BLUE;
    if (t < 26)
        return DISPLAY_MY_GREEN;
    if (t < 32)
        return DISPLAY_MY_YELLOW;
    if (t < 38)
        return DISPLAY_ORANGE;
    return DISPLAY_MY_RED;
}

UG_COLOR humColor(float h)
{
    if (h < 30)
        return DISPLAY_ORANGE;
    if (h <= 60)
        return DISPLAY_MY_GREEN;
    return DISPLAY_LIGHT_BLUE;
}

const char *const *pickWeatherIcon()
{
    if (strcmp(g_weather.main_desc, "Clear") == 0)
        return g_weather.is_night ? ICON_MOON : ICON_SUN;
    if (strcmp(g_weather.main_desc, "Clouds") == 0)
        return ICON_CLOUD;
    if (strcmp(g_weather.main_desc, "Rain") == 0 || strcmp(g_weather.main_desc, "Drizzle") == 0)
        return ICON_RAIN;
    if (strcmp(g_weather.main_desc, "Thunderstorm") == 0)
        return ICON_STORM;
    if (strcmp(g_weather.main_desc, "Snow") == 0)
        return ICON_SNOW;
    return ICON_MIST;
}

const char *const *pickFlightIcon()
{
    if (strcasestr(g_flight.category, "helicopter"))
        return ICON_HELICOPTER;
    if (strcasestr(g_flight.category, "piston"))
        return ICON_PISTON;
    if (strcasestr(g_flight.category, "turboprop"))
        return ICON_TURBOPROP;
    if (strcasestr(g_flight.category, "turbofan") || strcasestr(g_flight.category, "jet"))
        return ICON_TURBOFAN;
    return ICON_UNKNOWN;
}

// ================================================================
// DASHBOARD ZONES
// ================================================================
static void ringPoint(int inset, int idx, int *x, int *y)
{
    int edge = PANEL_RES_X - 2 * inset - 1;
    int total = 4 * edge;
    idx = (idx % total + total) % total;

    if (idx < edge)
    {
        *x = inset + idx;
        *y = inset;
    }
    else if (idx < 2 * edge)
    {
        *x = inset + edge;
        *y = inset + (idx - edge);
    }
    else if (idx < 3 * edge)
    {
        *x = inset + edge - (idx - 2 * edge);
        *y = inset + edge;
    }
    else
    {
        *x = inset;
        *y = inset + edge - (idx - 3 * edge);
    }
}

void drawBorder(bool animate)
{
    UG_DrawFrame(0, 0, 63, 63, DISPLAY_DEEP_BLUE);
    UG_DrawFrame(1, 1, 62, 62, DISPLAY_MY_BLUE);
    UG_DrawFrame(2, 2, 61, 61, DISPLAY_MY_CYAN);

    UG_DrawPixel(0, 0, DISPLAY_BLACK);
    UG_DrawPixel(63, 0, DISPLAY_BLACK);
    UG_DrawPixel(0, 63, DISPLAY_BLACK);
    UG_DrawPixel(63, 63, DISPLAY_BLACK);

    UG_DrawPixel(2, 2, DISPLAY_WHITE);
    UG_DrawPixel(61, 2, DISPLAY_WHITE);
    UG_DrawPixel(2, 61, DISPLAY_WHITE);
    UG_DrawPixel(61, 61, DISPLAY_WHITE);

    if (animate)
    {
        const int inset = 1;
        const int total = 4 * (64 - 2 * inset - 1);
        int head = ((int64_t)(esp_timer_get_time() / 1000) / 50) % total;
        for (int t = 4; t >= 0; t--)
        {
            int x, y;
            ringPoint(inset, head - t, &x, &y);
            UG_DrawPixel(x, y, COMET[t]);
        }
    }
}

void drawProvisioningQr(void)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    bool ready = g_provisioning_qr_ready;
    uint8_t size = g_provisioning_qr_size;
    uint8_t qr[PROVISIONING_QR_MAX_SIZE][PROVISIONING_QR_MAX_SIZE];
    memcpy(qr, g_provisioning_qr, sizeof(qr));
    xSemaphoreGive(data_mutex);

    if (!ready || size == 0 || size > PROVISIONING_QR_MAX_SIZE)
        return;

    const int quiet_zone = 4;
    const int scale = (PANEL_RES_X - quiet_zone * 2) / (size + quiet_zone * 2);
    const int qr_width = (size + quiet_zone * 2) * scale;
    const int origin_x = (PANEL_RES_X - qr_width) / 2;
    const int origin_y = 0;
    for (int y = 0; y < size + quiet_zone * 2; ++y)
    {
        for (int x = 0; x < size + quiet_zone * 2; ++x)
        {
            int qr_x = x - quiet_zone;
            int qr_y = y - quiet_zone;
            UG_COLOR color = (qr_x >= 0 && qr_y >= 0 && qr_x < size && qr_y < size &&
                              qr[qr_y][qr_x])
                                 ? DISPLAY_BLACK
                                 : DISPLAY_WHITE;
            UG_FillFrame(origin_x + x * scale, origin_y + y * scale,
                         origin_x + (x + 1) * scale - 1,
                         origin_y + (y + 1) * scale - 1, color);
        }
    }

    drawCentered("WIFI SETUP", 55, DISPLAY_MY_CYAN, false);
}

void drawWifiStartup(void)
{
    drawCentered("Smart", 20, DISPLAY_MY_CYAN, true);
    drawCentered("Matrix", 28, DISPLAY_MY_CYAN, true);
    drawCentered("CONNECTING", 38, DISPLAY_WHITE, false);
    drawCentered("WIFI...", 47, DISPLAY_LIGHT_BLUE, false);
}

void drawSeparators(int sec, bool showSep2)
{
    UG_FillFrame(IN_L, SEP1_Y, IN_L + IN_W - 1, SEP1_Y + 1, DISPLAY_SEP_DIM);
    if (sec >= 0)
    {
        int barLen = 1 + (sec * (IN_W - 1)) / 59;
        for (int i = 0; i < barLen; i++)
        {
            uint8_t r = 0;
            uint8_t g = 100 + (i * 120) / IN_W;
            uint8_t b = 255;
            UG_DrawLine(IN_L + i, SEP1_Y, IN_L + i, SEP1_Y + 1, (r << 16) | (g << 8) | b);
        }
        UG_DrawLine(IN_L + barLen - 1, SEP1_Y, IN_L + barLen - 1, SEP1_Y + 1, DISPLAY_WHITE);
    }
    if (showSep2)
    {
        UG_DrawLine(IN_L, SEP2_Y, IN_L + IN_W - 1, SEP2_Y, DISPLAY_MY_BLUE);
        UG_DrawLine(IN_L, SEP2_Y + 1, IN_L + IN_W - 1, SEP2_Y + 1, DISPLAY_SEP_DIM);
    }
}

void drawClockText(const char *s, int y, bool colonOn)
{
    int n = strlen(s);
    int w = 0;
    for (int i = 0; i < n; i++)
        w += (s[i] == ':') ? 5 : 7;
    if (n > 0 && s[n - 1] != ':')
        w -= 1;

    int x = IN_L + (IN_W - w) / 2;
    for (int i = 0; i < n; i++)
    {
        if (s[i] == ':')
        {
            UG_COLOR c = colonOn ? DISPLAY_WHITE : DISPLAY_COLON_DIM;
            UG_FillFrame(x + 1, y + 1, x + 2, y + 2, c); // 2x2 box at y+1
            UG_FillFrame(x + 1, y + 4, x + 2, y + 5, c); // 2x2 box at y+4
            x += 5;
        }
        else
        {
            drawGlyph(x, y, s[i], DISPLAY_MY_CYAN, true);
            x += 7;
        }
    }
}

void drawClockZone(const struct tm *ti)
{
    if (ti->tm_year < (2026 - 1900))
    {
        drawCentered("NO TIME", TIME_Y, DISPLAY_MY_RED, true);
        return;
    }

    int h12 = ti->tm_hour % 12;
    if (h12 == 0)
        h12 = 12;
    bool pm = (ti->tm_hour >= 12);

    char timeText[12];
    snprintf(timeText, sizeof(timeText), "%d:%02d:%02d", h12, ti->tm_min, ti->tm_sec);
    drawClockText(timeText, TIME_Y, (ti->tm_sec % 2) == 0);

    char dateText[10];
    snprintf(dateText, sizeof(dateText), "%d %s", ti->tm_mday, MONTHS[ti->tm_mon % 12]);

    const int pillW = 15;
    const int pillH = 9;
    int total = pillW + 3 + textWidth(dateText, false);
    int x0 = IN_L + (IN_W - total) / 2;
    UG_COLOR pillColor = pm ? DISPLAY_ORANGE : DISPLAY_MY_YELLOW;

    UG_FillFrame(x0 + 1, DATE_Y, x0 + pillW - 2, DATE_Y + pillH - 1, pillColor);
    UG_FillFrame(x0, DATE_Y + 1, x0 + pillW - 1, DATE_Y + pillH - 2, pillColor);
    drawString(x0 + 2, DATE_Y + 1, pm ? "PM" : "AM", DISPLAY_BLACK, false);

    drawString(x0 + pillW + 3, DATE_Y + 1, dateText, DISPLAY_LIGHT_BLUE, false);
}

void drawEnvironmentZone()
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    bool ok = g_shtc3_ok;
    float t = g_env_temp;
    float h = g_env_hum;
    xSemaphoreGive(data_mutex);

    if (!ok)
    {
        drawCentered("SENS ERR", 32, DISPLAY_MY_RED, false);
        return;
    }

    char num[10];
    drawIcon(ICON_CX - 2, ENV1_Y, ICON_THERMO);
    snprintf(num, sizeof(num), "%.1f", t);
    drawTempValue(TEXT_X, ENV1_Y, num, tempColor(t), false);

    drawIcon(ICON_CX - 2, ENV2_Y, ICON_DROP);
    snprintf(num, sizeof(num), "%.0f%%", h);
    drawString(TEXT_X, ENV2_Y, num, humColor(h), false);
}

void drawWeatherZone()
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    bool ok = g_weather.is_valid;
    float t = g_weather.temp;
    xSemaphoreGive(data_mutex);

    if (!ok)
    {
        drawCentered("NO DATA", WX_Y, DISPLAY_MY_RED, false);
        return;
    }

    char num[10];
    drawIcon(ICON_CX - 4, WX_Y, pickWeatherIcon());
    snprintf(num, sizeof(num), "%.0f", t);
    drawTempValue(TEXT_X, WX_Y, num, tempColor(t), true);
}

// Draw a string where the first `codeLen` chars use codeCol, the rest textCol.
void drawColoredString(int x, int y, const char *s, int codeLen, UG_COLOR codeCol, UG_COLOR textCol)
{
    for (int i = 0; s[i]; i++)
    {
        int cx = x + i * 6;
        if (cx > PANEL_RES_X - 1)
            break;
        if (cx < -6)
            continue;
        drawGlyph(cx, y, s[i], i < codeLen ? codeCol : textCol, false);
    }
}

void drawMarquee(int regionX, int regionW, int y, const char *str, int codeLen, UG_COLOR codeCol, UG_COLOR textCol, uint64_t elapsed)
{
    int w = textWidth(str, false);
    if (w <= regionW)
    {
        drawColoredString(regionX, y, str, codeLen, codeCol, textCol);
        return;
    }
    int period = w + 14; // SCROLL_GAP_PX
    int offset = 0;
    if (elapsed > 1500)
    {
        offset = (int)(((elapsed - 1500) * 18 / 1000) % period);
    }
    drawColoredString(regionX - offset, y, str, codeLen, codeCol, textCol);
    drawColoredString(regionX - offset + period, y, str, codeLen, codeCol, textCol);
}

void drawRouteLine(int y, const char *iata, const char *name, UG_COLOR codeCol, uint64_t elapsed)
{
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%s:", iata);
    int totalPrefixW = strlen(prefix) * 6;
    int nameX = IN_L + totalPrefixW;
    int nameW = IN_W - totalPrefixW;

    drawMarquee(nameX, nameW, y, name, 0, DISPLAY_WHITE, DISPLAY_WHITE, elapsed);
    UG_FillFrame(0, y, nameX - 1, y + 8, DISPLAY_BLACK);
    drawColoredString(IN_L, y, prefix, strlen(prefix), codeCol, codeCol);
}

void drawFlightZone(uint64_t el)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    drawMarquee(FL_TEXT_X, FL_TEXT_W, FL_CALL_Y, g_flight.callsign, 0, DISPLAY_MY_GREEN, DISPLAY_MY_GREEN, el);
    drawMarquee(FL_TEXT_X, FL_TEXT_W, FL_MODEL_Y, g_flight.model, 0, DISPLAY_LIGHT_BLUE, DISPLAY_LIGHT_BLUE, el);

    UG_FillFrame(IN_L, FL_ICON_Y + 1, IN_L + FL_ICON_MASK_W - 1, FL_ICON_Y + 19, DISPLAY_BLACK);
    drawIconN(FL_ICON_X, FL_ICON_Y, pickFlightIcon(), 20);

    UG_DrawLine(IN_L, FL_DIV_Y, IN_L + IN_W - 1, FL_DIV_Y, DISPLAY_SEP_DIM);

    drawRouteLine(FL_ORIGIN_Y, g_flight.originIata, g_flight.originName, DISPLAY_MY_GREEN, el);
    drawRouteLine(FL_DEST_Y, g_flight.destIata, g_flight.destName, DISPLAY_ORANGE, el);
    xSemaphoreGive(data_mutex);
}