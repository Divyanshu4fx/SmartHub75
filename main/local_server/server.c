#include "server.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#define BUFF_SIZE 512
#define WEATHER_URL_MAX 384
static char response_buf[BUFF_SIZE];
static int response_len = 0;

static void copy_url_value(char *destination, size_t capacity, const char *source)
{
    size_t offset = 0;
    for (size_t i = 0; source[i] != '\0' && offset + 1 < capacity; ++i)
    {
        if (source[i] == ' ')
        {
            if (offset + 3 >= capacity)
                break;
            destination[offset++] = '%';
            destination[offset++] = '2';
            destination[offset++] = '0';
        }
        else
            destination[offset++] = source[i];
    }
    destination[offset] = '\0';
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
    while (1)
    {
        app_settings_t settings;
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        settings = g_settings;
        xSemaphoreGive(data_mutex);

        char city[SETTINGS_CITY_MAX * 3];
        char url[WEATHER_URL_MAX];
        copy_url_value(city, sizeof(city), settings.city);
        int url_length = snprintf(url, sizeof(url),
                                  "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s",
                                  city, settings.api_key, OWM_UNITS);
        if (url_length < 0 || (size_t)url_length >= sizeof(url))
        {
            ESP_LOGE("weather", "Weather URL is too long for the configured settings");
            xEventGroupWaitBits(settings_events, SETTINGS_CHANGED_BIT, pdTRUE, pdFALSE,
                                pdMS_TO_TICKS(10 * 60 * 1000));
            continue;
        }
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        response_len = 0;
        memset(response_buf, 0, BUFF_SIZE);
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = client ? esp_http_client_perform(client) : ESP_FAIL;
        bool weather_request_ok = false;

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
                weather_request_ok = true;
                xSemaphoreGive(data_mutex);
                cJSON_Delete(json);
            }
        }
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        g_weather_online = weather_request_ok;
        xSemaphoreGive(data_mutex);
        if (client)
            esp_http_client_cleanup(client);
        xEventGroupWaitBits(settings_events, SETTINGS_CHANGED_BIT, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(10 * 60 * 1000));
    }
}

// ================================================================
// WEB SERVER
// ================================================================
static esp_err_t get_handler(httpd_req_t *req)
{
    static const char dashboard[] = R"html(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartHUB75 Dashboard</title>
<style>
:root{color-scheme:dark;font-family:Inter,system-ui,-apple-system,Segoe UI,sans-serif;background:#07111f;color:#e9f3ff}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 15% 0,#12345d 0,#07111f 42%,#040912 100%)}
main{width:min(720px,100%);margin:auto;padding:28px 18px 40px}.brand{display:flex;align-items:center;gap:13px;margin-bottom:28px}
.logo{display:grid;place-items:center;width:44px;height:44px;border-radius:14px;background:#0bd7ff;color:#03101d;font-size:22px;font-weight:900;box-shadow:0 0 28px #0bd7ff66}
h1{font-size:1.45rem;margin:0}.sub{color:#8fa9c8;font-size:.88rem;margin-top:3px}.card{background:#0d1b2cdd;border:1px solid #1c3957;border-radius:18px;padding:21px;margin:14px 0;box-shadow:0 12px 30px #0003}
.card-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:18px}.label{font-size:.78rem;letter-spacing:.12em;text-transform:uppercase;color:#8fa9c8;font-weight:700}
.value{font-size:2.8rem;font-weight:800;color:#fff}.unit{font-size:1rem;color:#8fa9c8;font-weight:600}.range{width:100%;accent-color:#0bd7ff;cursor:pointer}.scale{display:flex;justify-content:space-between;color:#718cae;font-size:.72rem;margin-top:8px}
.btn{border:1px solid #2a5277;background:#122a43;color:#dff5ff;border-radius:10px;padding:9px 14px;font-weight:700;cursor:pointer}.btn:hover{background:#1b3d5d;border-color:#0bd7ff}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px}.stat{background:#091524;border-radius:12px;padding:15px}.stat b{display:block;color:#fff;font-size:1.05rem;margin-top:7px}.online{color:#38e6a0!important}.offline{color:#ff7785!important}.flight{color:#ffd166!important}
.flight-detail{color:#dcecff;line-height:1.6;margin:0}.muted{color:#718cae}.hint{color:#8fa9c8;font-size:.82rem;line-height:1.5;margin:16px 0 0}.toast{min-height:20px;color:#38e6a0;font-size:.82rem;margin-top:12px}.field{width:100%;margin:7px 0 14px;padding:11px;border:1px solid #2a5277;border-radius:9px;background:#091524;color:#fff}
@media(max-width:450px){main{padding:22px 14px}.value{font-size:2.35rem}.grid{grid-template-columns:1fr}}
</style>
</head>
<body><main>
<header class="brand"><div class="logo">*</div><div><h1>SmartHUB75</h1><div class="sub">LED matrix control center</div></div></header>
<section class="card">
  <div class="card-head"><span class="label">Brightness</span><span><span id="value" class="value">--</span><span class="unit"> / 75</span></span></div>
  <input id="slider" class="range" type="range" min="1" max="75" value="75" aria-label="Display brightness">
  <div class="scale"><span>1 · Dim</span><span>Comfort</span><span>75 · Maximum</span></div>
  <div id="toast" class="toast"></div>
</section>
<section class="card"><div class="card-head"><span class="label">System status</span><button id="refresh" class="btn">Refresh</button></div>
  <div class="grid"><div class="stat"><span class="label">Time sync</span><b id="time">Loading...</b></div><div class="stat"><span class="label">Weather</span><b id="weather">Loading...</b></div></div>
  <p class="hint">Time is synchronized from the internet when available. Weather keeps the last valid reading if the service goes offline.</p>
</section>
<section class="card"><div class="card-head"><span class="label">Device settings</span></div>
  <label class="label" for="city">City</label>
  <input id="city" class="field" placeholder="Navi Mumbai,IN">
  <label class="label" for="timezone">Timezone</label>
  <input id="timezone" class="field" placeholder="IST-5:30">
  <label class="label" for="api">OpenWeather API key</label>
  <input id="api" class="field" type="password" placeholder="Leave blank to keep current key">
  <button id="save" class="btn">Save settings</button>
  <p class="hint">Use a POSIX timezone string, for example IST-5:30 or UTC0. Weather settings are stored in flash.</p>
</section>
<section class="card"><div class="card-head"><span class="label">Last flight</span><span id="flight-status" class="muted">No flight yet</span></div>
  <p id="flight" class="flight-detail"><span class="muted">No flight information received.</span></p>
</section>
</main>
<script>
const slider=document.getElementById('slider'),value=document.getElementById('value'),toast=document.getElementById('toast'),time=document.getElementById('time'),weather=document.getElementById('weather'),flight=document.getElementById('flight'),flightStatus=document.getElementById('flight-status'),city=document.getElementById('city'),timezone=document.getElementById('timezone'),api=document.getElementById('api');
let timer;
function show(v){value.textContent=v;slider.value=v}
async function setBrightness(v){show(v);clearTimeout(timer);timer=setTimeout(async()=>{try{let r=await fetch('/brightness',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({brightness:Number(v)})});if(!r.ok)throw Error();toast.textContent='Brightness updated';setTimeout(()=>toast.textContent='',1800)}catch(e){toast.textContent='Could not update brightness'}} ,120)}
async function refresh(){try{let r=await fetch('/api/status');let d=await r.json();show(d.brightness);city.value=d.city;timezone.value=d.timezone;time.textContent=d.time_synced?'Online':'Offline';time.className=d.time_synced?'online':'offline';weather.textContent=d.weather_online?'Online':'Offline';weather.className=d.weather_online?'online':'offline';flightStatus.textContent=d.flight_active?'Currently displaying':'Saved record';flightStatus.className=d.flight_active?'flight':'muted';flight.textContent='';if(d.last_flight){let name=document.createElement('b');name.textContent=d.last_flight.callsign;flight.append(name, ' | '+d.last_flight.model, document.createElement('br'), d.last_flight.origin+' -> '+d.last_flight.destination)}else{let empty=document.createElement('span');empty.className='muted';empty.textContent='No flight information received.';flight.append(empty)}}catch(e){time.textContent='Unavailable';weather.textContent='Unavailable'}}
async function saveSettings(){try{let r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({city:city.value,timezone:timezone.value,api_key:api.value})});if(!r.ok)throw Error();api.value='';toast.textContent='Settings saved';setTimeout(()=>toast.textContent='',1800)}catch(e){toast.textContent='Could not save settings'}}
slider.addEventListener('input',e=>setBrightness(e.target.value));document.getElementById('refresh').onclick=refresh;document.getElementById('save').onclick=saveSettings;refresh();
</script></body></html>
)html";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, dashboard, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t get_status_handler(httpd_req_t *req)
{
    bool flight_active, weather_online, time_synced;
    uint8_t brightness;
    flight_info_t last_flight;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    flight_active = g_flightActive;
    brightness = g_brightness;
    weather_online = g_weather_online;
    time_synced = g_time_synced;
    last_flight = g_flight;
    xSemaphoreGive(data_mutex);

    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(response_json, "brightness", brightness);
    cJSON_AddBoolToObject(response_json, "flight_active", flight_active);
    cJSON_AddBoolToObject(response_json, "weather_online", weather_online);
    cJSON_AddBoolToObject(response_json, "time_synced", time_synced);
    bool has_flight = last_flight.callsign[0] != '\0';
    app_settings_t settings;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    settings = g_settings;
    xSemaphoreGive(data_mutex);
    cJSON_AddBoolToObject(response_json, "has_last_flight", has_flight);
    cJSON_AddStringToObject(response_json, "city", settings.city);
    cJSON_AddStringToObject(response_json, "timezone", settings.timezone);
    if (has_flight)
    {
        cJSON *flight = cJSON_CreateObject();
        cJSON_AddStringToObject(flight, "callsign", last_flight.callsign);
        cJSON_AddStringToObject(flight, "model", last_flight.model);
        cJSON_AddStringToObject(flight, "origin", last_flight.originIata);
        cJSON_AddStringToObject(flight, "destination", last_flight.destIata);
        cJSON_AddItemToObject(response_json, "last_flight", flight);
    }
    char *response = cJSON_PrintUnformatted(response_json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    cJSON_free(response);
    cJSON_Delete(response_json);
    return ESP_OK;
}

static esp_err_t post_settings_handler(httpd_req_t *req)
{
    char buf[512];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\":\"settings JSON is required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *json = cJSON_Parse(buf);
    cJSON *city = json ? cJSON_GetObjectItem(json, "city") : NULL;
    cJSON *timezone = json ? cJSON_GetObjectItem(json, "timezone") : NULL;
    cJSON *api_key = json ? cJSON_GetObjectItem(json, "api_key") : NULL;
    if (!cJSON_IsString(city) || !cJSON_IsString(timezone) ||
        !cJSON_IsString(api_key) || city->valuestring[0] == '\0' ||
        timezone->valuestring[0] == '\0')
    {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"city and timezone are required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    app_settings_t settings;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    settings = g_settings;
    xSemaphoreGive(data_mutex);
    if (strlen(city->valuestring) >= sizeof(settings.city) ||
        strlen(timezone->valuestring) >= sizeof(settings.timezone) ||
        strlen(api_key->valuestring) >= sizeof(settings.api_key))
    {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\":\"one or more settings are too long\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(settings.city, sizeof(settings.city), "%s", city->valuestring);
    snprintf(settings.timezone, sizeof(settings.timezone), "%s", timezone->valuestring);
    if (api_key->valuestring[0] != '\0')
        snprintf(settings.api_key, sizeof(settings.api_key), "%s", api_key->valuestring);
    bool saved = settings_update(&settings);
    cJSON_Delete(json);
    if (!saved)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"settings are invalid or could not be saved\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_brightness_handler(httpd_req_t *req)
{
    char buf[96];
    int ret, remaining = req->content_len;
    if (remaining <= 0 || remaining >= sizeof(buf))
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\":\"brightness JSON is required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret != remaining)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\":\"incomplete request body\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    cJSON *brightness = json ? cJSON_GetObjectItem(json, "brightness") : NULL;
    if (!brightness || !cJSON_IsNumber(brightness) || brightness->valuedouble < 1 ||
        brightness->valuedouble > 75 || brightness->valuedouble != brightness->valueint)
    {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"brightness must be an integer from 1 to 75\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    g_brightness = (uint8_t)brightness->valueint;
    xSemaphoreGive(data_mutex);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_clear_handler(httpd_req_t *req)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    g_flightActive = false;
    xSemaphoreGive(data_mutex);
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

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t uri_get = {.uri = "/", .method = HTTP_GET, .handler = get_handler, .user_ctx = NULL};
        httpd_uri_t uri_status = {.uri = "/api/status", .method = HTTP_GET, .handler = get_status_handler, .user_ctx = NULL};
        httpd_uri_t uri_settings = {.uri = "/api/settings", .method = HTTP_POST, .handler = post_settings_handler, .user_ctx = NULL};
        httpd_uri_t uri_brightness = {.uri = "/brightness", .method = HTTP_POST, .handler = post_brightness_handler, .user_ctx = NULL};
        httpd_uri_t uri_flight = {.uri = "/flight", .method = HTTP_POST, .handler = post_flight_handler, .user_ctx = NULL};
        httpd_uri_t uri_clear = {.uri = "/clear", .method = HTTP_POST, .handler = post_clear_handler, .user_ctx = NULL};

        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_status);
        httpd_register_uri_handler(server, &uri_settings);
        httpd_register_uri_handler(server, &uri_brightness);
        httpd_register_uri_handler(server, &uri_flight);
        httpd_register_uri_handler(server, &uri_clear);
    }
    return server;
}