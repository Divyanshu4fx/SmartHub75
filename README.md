# SmartHUB75

SmartHUB75 is an ESP32-S3 smart dashboard for a 64x64 HUB75 RGB LED matrix. It combines an NTP-synchronized clock, local SHTC3 temperature and humidity readings, live OpenWeather data, and an animated flight-information overlay controlled through a local REST API.

The display is designed for glanceable information: the normal view shows time, date, indoor climate, and outdoor weather, while a flight event temporarily replaces the lower dashboard area with aircraft pixel art, callsign/model information, and scrolling origin and destination marquees.

<!-- IMAGE PLACEHOLDER: Replace with a hero photo of the completed SmartHUB75 display. -->
![SmartHUB75 hero image](docs/images/smarthub75-hero.jpg)

## Features

- 64x64 HUB75 RGB matrix rendering driven by the ESP32-S3
- NTP time synchronization with configurable POSIX timezone
- Local temperature and humidity from an SHTC3 sensor
- Live weather from OpenWeather, including condition, temperature, humidity, and wind data
- QR-code Wi-Fi provisioning through an on-device SoftAP
- Startup screen showing `SmartHUB75` and `CONNECTING WIFI...` when saved Wi-Fi credentials are being used
- Embedded browser dashboard for status, settings, and brightness control
- Persistent city, API key, and timezone settings in ESP-IDF NVS
- Double-buffered rendering, 8 MHz output clock, and a 120 Hz minimum panel refresh rate
- Animated border comet effect and weather/aircraft pixel icons
- Flight mode triggered by REST API, with category-specific aircraft artwork
- Scrolling route marquees for long airport and location names
- Automatic return to the normal dashboard after a 30-second flight display

<!-- IMAGE PLACEHOLDER: Add a screenshot of the normal clock/weather dashboard. -->
![Normal dashboard view](docs/images/dashboard-normal.jpg)

<!-- IMAGE PLACEHOLDER: Add a screenshot of the flight overlay with route marquees. -->
![Flight overlay view](docs/images/dashboard-flight.jpg)

## Hardware

### Main components

- [Waveshare RGB Matrix P2 64x64](https://docs.waveshare.com/RGB-Matrix-Px-64x64) LED panel
- [Waveshare ESP32-S3 RGB Matrix board](https://www.waveshare.com/esp32-s3-rgb-matrix.htm) as the controller
- SHTC3 temperature and humidity sensor
- 5 V power supply sized for the matrix brightness and expected load

The project is built around the Waveshare ESP32-S3 RGB Matrix board driving the Waveshare RGB Matrix P2 64x64 panel. The panel uses a HUB75 interface and the firmware is configured for a 64x64, standard two-scan layout. Refer to the [Waveshare panel documentation](https://docs.waveshare.com/RGB-Matrix-P2-64x64) and [controller board product page](https://www.waveshare.com/esp32-s3-rgb-matrix.htm) for the manufacturer specifications, connectors, and hardware details.

### Pin assignments

The current firmware uses the following ESP32-S3 GPIO assignments:

| HUB75 signal | ESP32-S3 GPIO |
| --- | ---: |
| R1 | 4 |
| G1 | 5 |
| B1 | 6 |
| R2 | 7 |
| G2 | 15 |
| B2 | 16 |
| A | 18 |
| B | 8 |
| C | 3 |
| D | 42 |
| E | 9 |
| LAT | 40 |
| OE | 2 |
| CLK | 41 |

The SHTC3 uses the ESP-IDF I2C master driver:

| I2C signal | ESP32-S3 GPIO |
| --- | ---: |
| SDA | 47 |
| SCL | 48 |

Verify the pinout, voltage levels, scan type, and power requirements for the exact panel and ESP32-S3 board before connecting hardware. Do not power the LED panel from the ESP32-S3 3.3 V rail.

<!-- IMAGE PLACEHOLDER: Add a labeled wiring diagram for the ESP32-S3, HUB75 panel, and SHTC3. -->
![Wiring diagram](docs/images/wiring-diagram.png)

## Software stack

SmartHUB75 is an ESP-IDF project written in C and C++. The main component uses:

- `esphome/esp-hub75` for DMA-backed HUB75 output
- `pedrominatel/shtc3` for the local climate sensor
- `espressif/cjson` for REST request and response handling
- `espressif/qrcode` for Wi-Fi provisioning display
- uGUI for lightweight framebuffer-based graphics and pixel rendering
- FreeRTOS tasks, semaphores, event groups, NVS, SNTP, and ESP HTTP server/client components from ESP-IDF

The dependency versions are declared in [`main/idf_component.yml`](main/idf_component.yml). The firmware entry point and display task are in [`main/DISPLAY_PROJECT.cpp`](main/DISPLAY_PROJECT.cpp).

## Prerequisites

1. Install ESP-IDF with ESP32-S3 support.
2. Set the `IDF_PATH` environment variable and export the ESP-IDF tools.
3. Connect the ESP32-S3 board over USB.
4. Connect the HUB75 panel and SHTC3 using the assignments above.
5. Obtain an OpenWeather API key if live weather is required.

The project has been built with the ESP-IDF CMake workflow. The repository contains generated build output, but a clean build should be created with the commands below.

## Build and flash

From the repository root:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py -p COM<n> flash monitor
```

Replace `COM<n>` with the serial port assigned to the ESP32-S3. Press `Ctrl+]` to exit the serial monitor.

To erase saved Wi-Fi credentials and application settings before flashing:

```powershell
idf.py -p COM<n> erase-flash
idf.py -p COM<n> flash monitor
```

Erasing flash removes the provisioned network and saved SmartHUB75 settings.

## First-time setup

1. Power on the device.
2. On first boot, SmartHUB75 starts a provisioning SoftAP and renders a Wi-Fi QR code on the LED panel.
3. Scan the QR code with a compatible ESP-IDF provisioning application, or connect to the displayed `SmartHUB75-XXXX` access point and complete provisioning.
4. After the device receives an IP address, open `http://<device-ip>/` in a browser.
5. Set the city, POSIX timezone, and OpenWeather API key in the Device settings section.
6. Refresh the dashboard and confirm that time synchronization and weather are online.

The default settings are:

| Setting | Default |
| --- | --- |
| City | `Navi Mumbai,IN` |
| Timezone | `IST-5:30` |
| Brightness | `75` |

Use a POSIX timezone string, such as `IST-5:30` or `UTC0`. The weather task refreshes data every ten minutes and also reacts to saved settings changes.

<!-- IMAGE PLACEHOLDER: Add a photo or screenshot showing the QR provisioning screen. -->
![Wi-Fi provisioning QR code](docs/images/wifi-provisioning.jpg)

## Browser dashboard

Open the device IP address in a browser to use the built-in SmartHUB75 control center. It provides:

- Brightness control from 1 to 75
- NTP synchronization status
- Weather connectivity status
- City and timezone configuration
- OpenWeather API key update
- Current or most recently received flight information

The dashboard is served directly by the ESP32-S3; it does not require a separate web server or mobile application.

## REST API

All endpoints are served by the device's local HTTP server. Replace `<device-ip>` with the address assigned by the router.

### Read device status

```powershell
curl http://<device-ip>/api/status
```

Example response:

```json
{
  "brightness": 75,
  "flight_active": false,
  "weather_online": true,
  "time_synced": true,
  "has_last_flight": true,
  "city": "Navi Mumbai,IN",
  "timezone": "IST-5:30",
  "last_flight": {
    "callsign": "AI123",
    "model": "Boeing 787",
    "origin": "BOM",
    "destination": "DEL"
  }
}
```

### Update brightness

Brightness must be an integer from 1 to 75:

```powershell
curl -X POST http://<device-ip>/brightness `
  -H "Content-Type: application/json" `
  -d "{\"brightness\":45}"
```

### Save settings

The city and timezone are required. An empty `api_key` keeps the currently stored API key when using the browser dashboard; send a non-empty key when configuring the device for the first time.

```powershell
curl -X POST http://<device-ip>/api/settings `
  -H "Content-Type: application/json" `
  -d "{\"city\":\"Navi Mumbai,IN\",\"timezone\":\"IST-5:30\",\"api_key\":\"YOUR_OPENWEATHER_API_KEY\"}"
```

Settings are validated and saved to NVS. A successful update also causes the weather task to refresh using the new values.

### Trigger flight mode

Posting a flight event stores the latest flight record and displays it for 30 seconds:

```powershell
curl -X POST http://<device-ip>/flight `
  -H "Content-Type: application/json" `
  -d "{\"callsign\":\"AI123\",\"aircraft_code\":\"B789\",\"aircraft_model\":\"Boeing 787\",\"aircraft_category\":\"turbofan\",\"origin_iata\":\"BOM\",\"origin_name\":\"Mumbai\",\"destination_iata\":\"DEL\",\"destination_name\":\"New Delhi\"}"
```

Supported request fields:

| JSON field | Purpose |
| --- | --- |
| `callsign` | Flight callsign shown on the matrix |
| `aircraft_code` | Fallback model/code |
| `aircraft_model` | Aircraft model shown on the matrix |
| `aircraft_category` | Selects aircraft pixel art, such as `helicopter`, `piston`, `turboprop`, or `turbofan` |
| `origin_iata` / `origin_name` | Origin airport code and scrolling name |
| `destination_iata` / `destination_name` | Destination airport code and scrolling name |

The API response includes `"display_seconds":30`.

### Clear flight mode

```powershell
curl -X POST http://<device-ip>/clear
```

This immediately returns the display to the clock, climate, and weather layout.

<!-- IMAGE PLACEHOLDER: Add a diagram or screenshot illustrating the REST API to display flow. -->
![REST API flow](docs/images/rest-api-flow.png)

## Display behavior

The normal 64x64 layout contains:

1. A 12-hour clock with seconds and AM/PM indicator
2. The current day and month
3. Indoor temperature and humidity
4. A separator with a seconds progress indicator
5. Outdoor weather temperature and condition icon
6. An animated blue border accent

When flight mode is active, the lower content is replaced by:

- Category-specific aircraft artwork
- Callsign and aircraft model
- Origin and destination IATA codes
- Scrolling airport/location names

If provisioning is active, the display pauses on a stable QR frame until Wi-Fi setup completes.

## Project structure

```text
.
├── main/
│   ├── DISPLAY_PROJECT.cpp       # Application entry point and display task
│   ├── app_state.c/.h            # Shared runtime state and synchronization
│   ├── settings.c/.h             # NVS-backed configuration
│   ├── local_server/              # Browser dashboard and REST API
│   ├── sensor/                    # SHTC3 sampling task
│   ├── ui/                        # 64x64 drawing, icons, and layout
│   └── wifi/                      # Wi-Fi provisioning and SNTP setup
├── managed_components/            # ESP-IDF component manager dependencies
├── CMakeLists.txt
├── dependencies.lock
└── sdkconfig
```

## Safety and power notes

RGB matrix panels can draw substantial current, especially at high brightness. Use an appropriately rated 5 V supply, common the grounds, add strain relief, and avoid powering the panel through the ESP32-S3 development board. Reduce brightness during initial testing and inspect connectors for heat before installing the display permanently.

## License

SmartHUB75 is released under the [MIT License](LICENSE). Copyright © 2026 Divyanshu4fx.
