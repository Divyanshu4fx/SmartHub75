#include <string.h>
#include <stdlib.h>
#include "time.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "cJSON.h"
#include "shtc3.h"
#include "hub75.h"
extern "C"
{
#include "ugui.h"
}
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define PANEL_RES_X 64
#define PANEL_RES_Y 64

// ================================================================
// WIFI / WEATHER SETTINGS
// ================================================================
#define WIFI_SSID "Airtel_divy_6706"
#define WIFI_PASSWORD "Air@33932"
#define WIFI_MAX_RETRY 5

#define OWM_API_KEY "4c5900b3563744c20a321add8ef2ac8c"
#define OWM_CITY "Navi%20Mumbai,IN"
#define OWM_UNITS "metric"

#define BUFF_SIZE 512
static char response_buf[BUFF_SIZE];
static int response_len = 0;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static Hub75Driver *g_driver = nullptr;
static const char *TAG = "DISPLAY_PROJECT";
static int s_retry_num = 0;

// ================================================================
// SHARED DATA (Protected by Mutexes where necessary)
// ================================================================
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

static weather_data_t g_weather = {.is_valid = false};
static float g_env_temp = 0.0f;
static float g_env_hum = 0.0f;
static bool g_shtc3_ok = false;

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

static flight_info_t g_flight;
static char g_originLine[128];
static char g_destLine[128];
static bool g_flightActive = false;
static uint64_t g_flightStartMs = 0;
const uint64_t FLIGHT_DISPLAY_MS = 30000;

static SemaphoreHandle_t data_mutex;

// ================================================================
// COLORS & LAYOUT
// ================================================================
#define C_BLACK 0x000000
#define C_WHITE 0xFFFFFF
#define C_DEEP_BLUE 0x00196E
#define C_MY_BLUE 0x0050FF
#define C_LIGHT_BLUE 0x28A0FF
#define C_MY_CYAN 0x00DCFF
#define C_MY_GREEN 0x00FF64
#define C_MY_YELLOW 0xFFBE00
#define C_ORANGE 0xFF6E14
#define C_MY_RED 0xFF2828
#define C_MY_GRAY 0x96A5BE
#define C_ICON_BLUE 0x4696FF
#define C_SNOW 0xEBF5FF
#define C_MOON 0xFFEBA0
#define C_SEP_DIM 0x001E5A
#define C_COLON_DIM 0x003C8C

static const UG_COLOR COMET[5] = {C_WHITE, 0x96F0FF, 0x3CC8FF, 0x148CFF, 0x0A64FF};

static const char *const MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* const DAYS[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

#define IN_L 3
#define IN_W 58

#define TIME_Y 5
#define DATE_Y 13
#define SEP1_Y 24
#define ENV1_Y 28
#define ENV2_Y 37
#define SEP2_Y 46
#define WX_Y 51

#define ICON_CX 12
#define TEXT_X 19

#define FL_ICON_X 3
#define FL_ICON_Y 25
#define FL_ICON_MASK_W 23
#define FL_TEXT_X 26
#define FL_TEXT_W 35
#define FL_CALL_Y 27
#define FL_MODEL_Y 37
#define FL_DIV_Y 45
#define FL_ORIGIN_Y 46
#define FL_DEST_Y 54

// ================================================================
// ICON BITMAPS  ('.' = transparent, letters = palette colours)
//   Y yellow  O orange  W white  G gray  B blue  R red
//   S snow    M moon    P rotor/prop (light)  p rotor/prop (dim)
// ================================================================
static const char* const ICON_THERMO[7] = {
  "..G..",
  "..R..",
  "..R..",
  "..R..",
  ".RRR.",
  "RRRRR",
  ".RRR."
};

static const char* const ICON_DROP[7] = {
  "..B..",
  "..B..",
  ".BBB.",
  ".BBB.",
  "BWBBB",
  "BBBBB",
  ".BBB."
};

static const char* const ICON_SUN[7] = {
  "....Y....",
  "..Y.Y.Y..",
  "...OOO...",
  ".YYOOOYY.",
  "...OOO...",
  "..Y.Y.Y..",
  "....Y...."
};

static const char* const ICON_MOON[7] = {
  "..MMMM...",
  ".MMM.....",
  "MMM....Y.",
  "MMM......",
  "MMM......",
  ".MMM.....",
  "..MMMM..."
};

static const char* const ICON_CLOUD[7] = {
  ".........",
  "..GGG.GG.",
  ".GGGGGGGG",
  "GGGGGGGGG",
  "GGGGGGGGG",
  ".GGGGGGG.",
  "........."
};

static const char* const ICON_RAIN[7] = {
  "..GGG.GG.",
  ".GGGGGGGG",
  "GGGGGGGGG",
  "GGGGGGGGG",
  ".GGGGGGG.",
  ".B..B..B.",
  "B..B..B.."
};

static const char* const ICON_STORM[7] = {
  "..GGG.GG.",
  ".GGGGGGGG",
  "GGGGGGGGG",
  ".GGGYYGG.",
  "...YY....",
  "....YY...",
  "...Y....."
};

static const char* const ICON_SNOW[7] = {
  "..GGG.GG.",
  ".GGGGGGGG",
  "GGGGGGGGG",
  "GGGGGGGGG",
  ".GGGGGGG.",
  ".S..S..S.",
  "..S..S..S"
};

static const char* const ICON_MIST[7] = {
  ".........",
  ".GGGGGGG.",
  ".........",
  "GGGGGGGGG",
  ".........",
  ".GGGGGGG.",
  "........."
};

static const char* const ICON_HELICOPTER[20] = {
  ".....................",
  "...p.............p...",
  "....P.....O.....P....",
  ".....P..OOOOO..P.....",
  "......POOOOOOOP......",
  ".......POOOOOP.......",
  ".......OPOOOPO.......",
  ".......OOPOPOO.......",
  ".......OOOPOOO.......",
  ".......OOPOPOO.......",
  ".......OPOOOPO.......",
  ".......POOOOOP.......",
  "......P.OOOOO.P......",
  ".....P....O....P.....",
  "....P.....O.....P....",
  "..........O..........",
  "..........O.p........",
  "..........OOp........",
  "............P........",
  "....................."
};

static const char* const ICON_PISTON[20] = {
  ".....................",
  ".....................",
  ".......PPPPPPP.......",
  "..........O..........",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  "...OOOOOOOOOOOOOOO...",
  "..OOOOOOOOOOOOOOOOO..",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  "......OOOOOOOOO......",
  ".....OOOOOOOOOOO.....",
  ".....OOOOOOOOOOO.....",
  ".........OOO.........",
  "..........O..........",
  "....................."
};

static const char* const ICON_TURBOPROP[20] = {
  ".....................",
  "..........O..........",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  "...PPP...OOO...PPP...",
  "....O....OOO....O....",
  ".OOOOOOOOOOOOOOOOOOO.",
  ".OOOOOOOOOOOOOOOOOOO.",
  ".OOOOOOOOOOOOOOOOOOO.",
  "....O....OOO....O....",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  "......OOOOOOOOO......",
  "......OOOOOOOOO......",
  "..........O..........",
  ".....................",
  "....................."
};

static const char* const ICON_TURBOFAN[20] = {
  ".....................",
  "..........O..........",
  ".........OOO.........",
  ".........OOO.........",
  "........OOOOO........",
  "........OOOOO........",
  "........OOOOO........",
  "......OOOOOOOOO......",
  ".....OOOOOOOOOOO.....",
  "....OOOOOOOOOOOOO....",
  "...OOOOOOOOOOOOOOO...",
  ".OOOOOOOOOOOOOOOOOOO.",
  ".........OOO.........",
  ".........OOO.........",
  ".........ORO.........",
  ".........ORO.........",
  "........OOROO........",
  ".......OOOROOO.......",
  ".......OOOROOO.......",
  "..........R.........."
};

static const char* const ICON_UNKNOWN[20] = {
  ".....................",
  ".....................",
  ".....................",
  ".......OOOOOOO.......",
  "......OOOOOOOOO......",
  ".....OOO.....OOO.....",
  ".....OOO.....OOO.....",
  "............OOO......",
  "..........OOO........",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  ".........OOO.........",
  ".....................",
  ".........OOO.........",
  ".........OOO.........",
  ".....................",
  ".....................",
  ".....................",
  "....................."
};

// ================================================================
// ADAFRUIT GFX 5x7 CLASSIC FONT (ASCII 32-126)
// ================================================================
static const uint8_t font5x7[96][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5f, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7f, 0x14, 0x7f, 0x14}, {0x24, 0x2a, 0x7f, 0x2a, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1c, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1c, 0x00}, {0x14, 0x08, 0x3e, 0x08, 0x14}, {0x08, 0x08, 0x3e, 0x08, 0x08}, {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14}, {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01}, {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7f, 0x41, 0x41, 0x00}, {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7f, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40}, {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78}, {0x7f, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20}, {0x38, 0x44, 0x44, 0x48, 0x7f}, {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7e, 0x09, 0x01, 0x02}, {0x0c, 0x52, 0x52, 0x52, 0x3e}, {0x7f, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7d, 0x40, 0x00}, {0x20, 0x40, 0x44, 0x3d, 0x00}, {0x7f, 0x10, 0x28, 0x44, 0x00}, {0x00, 0x41, 0x7f, 0x40, 0x00}, {0x7c, 0x04, 0x18, 0x04, 0x78}, {0x7c, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38}, {0x7c, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7c}, {0x7c, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20}, {0x04, 0x3f, 0x44, 0x40, 0x20}, {0x3c, 0x40, 0x40, 0x20, 0x7c}, {0x1c, 0x20, 0x40, 0x20, 0x1c}, {0x3c, 0x40, 0x30, 0x40, 0x3c}, {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0c, 0x50, 0x50, 0x50, 0x3c}, {0x44, 0x64, 0x54, 0x4c, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00}, {0x00, 0x00, 0x7f, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00}, {0x10, 0x08, 0x08, 0x10, 0x08}, {0x00, 0x00, 0x00, 0x00, 0x00}};

// ================================================================
// LOW LEVEL DISPLAY WRAPPERS
// ================================================================
void pset(UG_S16 x, UG_S16 y, UG_COLOR c)
{
    if (x < 0 || x > 63 || y < 0 || y > 63)
        return;
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    g_driver->set_pixel(x, y, r, g, b);
}

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
        return C_MY_YELLOW;
    case 'O':
        return C_ORANGE;
    case 'W':
        return C_WHITE;
    case 'G':
        return C_MY_GRAY;
    case 'B':
        return C_ICON_BLUE;
    case 'R':
        return C_MY_RED;
    case 'S':
        return C_SNOW;
    case 'M':
        return C_MOON;
    case 'P':
        return C_SNOW;
    case 'p':
        return C_MY_GRAY;
    default:
        return C_WHITE;
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
        return C_LIGHT_BLUE;
    if (t < 26)
        return C_MY_GREEN;
    if (t < 32)
        return C_MY_YELLOW;
    if (t < 38)
        return C_ORANGE;
    return C_MY_RED;
}

UG_COLOR humColor(float h)
{
    if (h < 30)
        return C_ORANGE;
    if (h <= 60)
        return C_MY_GREEN;
    return C_LIGHT_BLUE;
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
void ringPoint(int inset, int idx, int &x, int &y)
{
    int edge = PANEL_RES_X - 2 * inset - 1;
    int total = 4 * edge;
    idx = (idx % total + total) % total;

    if (idx < edge)
    {
        x = inset + idx;
        y = inset;
    }
    else if (idx < 2 * edge)
    {
        x = inset + edge;
        y = inset + (idx - edge);
    }
    else if (idx < 3 * edge)
    {
        x = inset + edge - (idx - 2 * edge);
        y = inset + edge;
    }
    else
    {
        x = inset;
        y = inset + edge - (idx - 3 * edge);
    }
}

void drawBorder(bool animate)
{
    UG_DrawFrame(0, 0, 63, 63, C_DEEP_BLUE);
    UG_DrawFrame(1, 1, 62, 62, C_MY_BLUE);
    UG_DrawFrame(2, 2, 61, 61, C_MY_CYAN);

    UG_DrawPixel(0, 0, C_BLACK);
    UG_DrawPixel(63, 0, C_BLACK);
    UG_DrawPixel(0, 63, C_BLACK);
    UG_DrawPixel(63, 63, C_BLACK);

    UG_DrawPixel(2, 2, C_WHITE);
    UG_DrawPixel(61, 2, C_WHITE);
    UG_DrawPixel(2, 61, C_WHITE);
    UG_DrawPixel(61, 61, C_WHITE);

    if (animate)
    {
        const int inset = 1;
        const int total = 4 * (64 - 2 * inset - 1);
        int head = ((int64_t)(esp_timer_get_time() / 1000) / 50) % total;
        for (int t = 4; t >= 0; t--)
        {
            int x, y;
            ringPoint(inset, head - t, x, y);
            UG_DrawPixel(x, y, COMET[t]);
        }
    }
}

void drawSeparators(int sec, bool showSep2)
{
    UG_FillFrame(IN_L, SEP1_Y, IN_L + IN_W - 1, SEP1_Y + 1, C_SEP_DIM);
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
        UG_DrawLine(IN_L + barLen - 1, SEP1_Y, IN_L + barLen - 1, SEP1_Y + 1, C_WHITE);
    }
    if (showSep2)
    {
        UG_DrawLine(IN_L, SEP2_Y, IN_L + IN_W - 1, SEP2_Y, C_MY_BLUE);
        UG_DrawLine(IN_L, SEP2_Y + 1, IN_L + IN_W - 1, SEP2_Y + 1, C_SEP_DIM);
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
            UG_COLOR c = colonOn ? C_WHITE : C_COLON_DIM;
            UG_FillFrame(x + 1, y + 1, x + 2, y + 2, c); // 2x2 box at y+1
            UG_FillFrame(x + 1, y + 4, x + 2, y + 5, c); // 2x2 box at y+4
            x += 5;
        }
        else
        {
            drawGlyph(x, y, s[i], C_MY_CYAN, true);
            x += 7;
        }
    }
}

void drawClockZone(const struct tm *ti)
{
    if (ti->tm_year < (2026 - 1900))
    {
        drawCentered("NO TIME", TIME_Y, C_MY_RED, true);
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
    UG_COLOR pillColor = pm ? C_ORANGE : C_MY_YELLOW;

    UG_FillFrame(x0 + 1, DATE_Y, x0 + pillW - 2, DATE_Y + pillH - 1, pillColor);
    UG_FillFrame(x0, DATE_Y + 1, x0 + pillW - 1, DATE_Y + pillH - 2, pillColor);
    drawString(x0 + 2, DATE_Y + 1, pm ? "PM" : "AM", C_BLACK, false);

    drawString(x0 + pillW + 3, DATE_Y + 1, dateText, C_LIGHT_BLUE, false);
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
        drawCentered("SENS ERR", 32, C_MY_RED, false);
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
        drawCentered("NO DATA", WX_Y, C_MY_RED, false);
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

    drawMarquee(nameX, nameW, y, name, 0, C_WHITE, C_WHITE, elapsed);
    UG_FillFrame(0, y, nameX - 1, y + 8, C_BLACK);
    drawColoredString(IN_L, y, prefix, strlen(prefix), codeCol, codeCol);
}

void drawFlightZone(uint64_t el)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    drawMarquee(FL_TEXT_X, FL_TEXT_W, FL_CALL_Y, g_flight.callsign, 0, C_MY_GREEN, C_MY_GREEN, el);
    drawMarquee(FL_TEXT_X, FL_TEXT_W, FL_MODEL_Y, g_flight.model, 0, C_LIGHT_BLUE, C_LIGHT_BLUE, el);

    UG_FillFrame(IN_L, FL_ICON_Y + 1, IN_L + FL_ICON_MASK_W - 1, FL_ICON_Y + 19, C_BLACK);
    drawIconN(FL_ICON_X, FL_ICON_Y, pickFlightIcon(), 20);

    UG_DrawLine(IN_L, FL_DIV_Y, IN_L + IN_W - 1, FL_DIV_Y, C_SEP_DIM);

    drawRouteLine(FL_ORIGIN_Y, g_flight.originIata, g_flight.originName, C_MY_GREEN, el);
    drawRouteLine(FL_DEST_Y, g_flight.destIata, g_flight.destName, C_ORANGE, el);
    xSemaphoreGive(data_mutex);
}

// ================================================================
// TASKS
// ================================================================

void setup_time(void)
{
    setenv("TZ", "IST-5:30", 1);
    tzset();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2026 - 1900))
    {
        ESP_LOGI(TAG, "Setting time from Internet...");
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&config);
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) != ESP_OK)
            ESP_LOGE(TAG, "Failed to sync time via SNTP timeout(5s)");
    }
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

void initialize_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    wifi_config_t wifi_config = {.sta = {.ssid = WIFI_SSID, .password = WIFI_PASSWORD}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA)
    {
        int copy = event->data_len;
        if (response_len + copy >= BUFF_SIZE)
            copy = BUFF_SIZE - response_len - 1;
        memcpy(response_buf + response_len, event->data, copy);
        response_len += copy;
        response_buf[response_len] = '\0';
    }
    return ESP_OK;
}

void http_get_weather_task(void *arg)
{
    char url[256];
    snprintf(url, sizeof(url), "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s", OWM_CITY, OWM_API_KEY, OWM_UNITS);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    while (1)
    {
        response_len = 0;
        memset(response_buf, 0, BUFF_SIZE);
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK && esp_http_client_get_status_code(client) == 200)
        {
            cJSON *json = cJSON_Parse(response_buf);
            if (json != NULL)
            {
                xSemaphoreTake(data_mutex, portMAX_DELAY);
                cJSON *weather_arr = cJSON_GetObjectItem(json, "weather");
                if (cJSON_GetArraySize(weather_arr) > 0)
                {
                    cJSON *weather_item = cJSON_GetArrayItem(weather_arr, 0);
                    snprintf(g_weather.main_desc, sizeof(g_weather.main_desc), "%s", cJSON_GetObjectItem(weather_item, "main")->valuestring);
                    snprintf(g_weather.icon, sizeof(g_weather.icon), "%s", cJSON_GetObjectItem(weather_item, "icon")->valuestring);
                    g_weather.is_night = (strlen(g_weather.icon) >= 3 && g_weather.icon[2] == 'n');
                }
                cJSON *main_obj = cJSON_GetObjectItem(json, "main");
                g_weather.temp = cJSON_GetObjectItem(main_obj, "temp")->valuedouble;
                g_weather.humidity = cJSON_GetObjectItem(main_obj, "humidity")->valueint;
                // Add this block to parse wind:
                cJSON *wind_obj = cJSON_GetObjectItem(json, "wind");
                if (wind_obj)
                {
                    cJSON *speed_obj = cJSON_GetObjectItem(wind_obj, "speed");
                    if (speed_obj)
                        g_weather.wind_speed = speed_obj->valuedouble;
                }
                g_weather.is_valid = true;
                xSemaphoreGive(data_mutex);
                cJSON_Delete(json);
            }
        }
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(10 * 60 * 1000)); // Every 10 mins
    }
}

void local_temp_humdity_task(void *arg)
{
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = -1, .sda_io_num = GPIO_NUM_47, .scl_io_num = GPIO_NUM_48, .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags = {.enable_internal_pullup = true}};
    i2c_master_bus_handle_t i2c_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_handle));
    i2c_master_dev_handle_t shtc3_handle = shtc3_device_create(i2c_handle, SHTC3_I2C_ADDR, 400000);

    float t = 0.0f, h = 0.0f;
    while (1)
    {
        if (shtc3_get_th(shtc3_handle, SHTC3_REG_T_CSE_NM, &t, &h) == ESP_OK)
        {
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            g_env_temp = t;
            g_env_hum = h;
            g_shtc3_ok = true;
            xSemaphoreGive(data_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ================================================================
// WEB SERVER (Flight POSTs)
// ================================================================
static esp_err_t get_handler(httpd_req_t *req)
{
    const char *resp = g_flightActive ? "Matrix dashboard online\nFlight mode: ACTIVE" : "Matrix dashboard online\nFlight mode: idle";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_clear_handler(httpd_req_t *req)
{
    g_flightActive = false;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_flight_handler(httpd_req_t *req)
{
    char buf[512];
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf))
        return ESP_FAIL;

    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0)
        return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    snprintf(g_flight.callsign, sizeof(g_flight.callsign), "%s", cJSON_GetObjectItem(json, "callsign") ? cJSON_GetObjectItem(json, "callsign")->valuestring : "");
    snprintf(g_flight.aircraftCode, sizeof(g_flight.aircraftCode), "%s", cJSON_GetObjectItem(json, "aircraft_code") ? cJSON_GetObjectItem(json, "aircraft_code")->valuestring : "");
    snprintf(g_flight.model, sizeof(g_flight.model), "%s", cJSON_GetObjectItem(json, "aircraft_model") ? cJSON_GetObjectItem(json, "aircraft_model")->valuestring : g_flight.aircraftCode);
    snprintf(g_flight.category, sizeof(g_flight.category), "%s", cJSON_GetObjectItem(json, "aircraft_category") ? cJSON_GetObjectItem(json, "aircraft_category")->valuestring : "");
    snprintf(g_flight.originIata, sizeof(g_flight.originIata), "%s", cJSON_GetObjectItem(json, "origin_iata") ? cJSON_GetObjectItem(json, "origin_iata")->valuestring : "");
    snprintf(g_flight.originName, sizeof(g_flight.originName), "%s", cJSON_GetObjectItem(json, "origin_name") ? cJSON_GetObjectItem(json, "origin_name")->valuestring : "");
    snprintf(g_flight.destIata, sizeof(g_flight.destIata), "%s", cJSON_GetObjectItem(json, "destination_iata") ? cJSON_GetObjectItem(json, "destination_iata")->valuestring : "");
    snprintf(g_flight.destName, sizeof(g_flight.destName), "%s", cJSON_GetObjectItem(json, "destination_name") ? cJSON_GetObjectItem(json, "destination_name")->valuestring : "");

    g_flightActive = true;
    g_flightStartMs = esp_timer_get_time() / 1000;
    xSemaphoreGive(data_mutex);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"display_seconds\":30}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t uri_get = {.uri = "/", .method = HTTP_GET, .handler = get_handler, .user_ctx = NULL};
        httpd_uri_t uri_flight = {.uri = "/flight", .method = HTTP_POST, .handler = post_flight_handler, .user_ctx = NULL};
        httpd_uri_t uri_clear = {.uri = "/clear", .method = HTTP_POST, .handler = post_clear_handler, .user_ctx = NULL};

        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_flight);
        httpd_register_uri_handler(server, &uri_clear);
    }
    return server;
}

// ================================================================
// MAIN DISPLAY TASK
// ================================================================
void display_task(void *arg)
{
    Hub75Config config{};
    config.panel_width = 64;
    config.panel_height = 64;
    config.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
    config.shift_driver = Hub75ShiftDriver::FM6126A;
    config.layout_rows = 1;
    config.layout_cols = 1;
    config.layout = Hub75PanelLayout::HORIZONTAL;
    config.pins.r1 = 4;
    config.pins.g1 = 5;
    config.pins.b1 = 6;
    config.pins.r2 = 7;
    config.pins.g2 = 15;
    config.pins.b2 = 16;
    config.pins.a = 18;
    config.pins.b = 8;
    config.pins.c = 3;
    config.pins.d = 42;
    config.pins.e = 9;
    config.pins.lat = 40;
    config.pins.oe = 2;
    config.pins.clk = 41;
    config.output_clock_speed = Hub75ClockSpeed::HZ_8M;
    config.min_refresh_rate = 60;

    // Enabling double buffering to completely eliminate flicker
    config.double_buffer = true;
    config.brightness = 5;

    Hub75Driver driver(config);
    g_driver = &driver;
    if (!driver.begin())
    {
        ESP_LOGE(TAG, "HUB75 init failed");
        vTaskDelete(NULL);
    }

    UG_GUI gui;
    UG_Init(&gui, pset, 64, 64);

    while (1)
    {
        uint64_t nowMs = esp_timer_get_time() / 1000;

        if (g_flightActive && (nowMs - g_flightStartMs >= FLIGHT_DISPLAY_MS))
        {
            g_flightActive = false;
        }

        time_t now;
        struct tm ti;
        time(&now);
        localtime_r(&now, &ti);

        UG_FillScreen(C_BLACK);
        drawClockZone(&ti);

        if (g_flightActive)
        {
            drawFlightZone(nowMs - g_flightStartMs);
        }
        else
        {
            drawEnvironmentZone();
            drawWeatherZone();
        }

        drawSeparators(ti.tm_sec, !g_flightActive);
        drawBorder(true);

        // Swap back-buffer to screen
        g_driver->flip_buffer();

        vTaskDelay(pdMS_TO_TICKS(33)); // ~30fps
    }
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    data_mutex = xSemaphoreCreateMutex();
    initialize_wifi();
    setup_time();
    start_webserver();

    xTaskCreate(http_get_weather_task, "weather_task", 4096, NULL, 5, NULL);
    xTaskCreate(local_temp_humdity_task, "shtc3_task", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 16384, NULL, 5, NULL);
}
