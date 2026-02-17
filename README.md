# WiFi Thermometer - ESP32 WROOM

PlatformIO project for reading DS18B20 temperature sensor on an ESP32 WROOM
and sending data to https://wifitemp.jpmac.com.

## Hardware

### Components
- ESP32 WROOM development board
- DS18B20 digital temperature sensor
- 4.7kΩ pull-up resistor

### Wiring

```
ESP32 WROOM              DS18B20
──────────                ───────
3.3V (any)  ──────────── VDD (Red)
GND  (any)  ──────────── GND (Black)
GPIO4       ──────────── DATA (Yellow)

4.7kΩ resistor between 3.3V and GPIO4
```

**Pin reference on ESP32 WROOM:**
```
┌───────────────────┐
│  ESP32 WROOM-32   │
│                   │
│  3.3V  ●          │  ← Power for sensor
│  GND   ●          │  ← Ground for sensor
│  GPIO4 ●          │  ← Data line (with 4.7kΩ pull-up)
│  GPIO2 ●          │  ← Onboard LED (status indicator)
└───────────────────┘
```

> **Note:** You can use any GPIO pin. Update `ONE_WIRE_PIN` in `config.h` if you use a different pin.

## Software Setup

### 1. Install PlatformIO

Install the [PlatformIO extension](https://platformio.org/install/ide?install=vscode) for VSCode.

### 2. Clone/Open Project

Open the `ESP32_Thermometer` folder in VSCode with PlatformIO.

### 3. Configure WiFi and Server

Edit `include/config.h`:

```cpp
#define WIFI_SSID       "YourNetworkName"
#define WIFI_PASSWORD   "YourPassword"
#define SERVER_URL      "https://wifitemp.jpmac.com"
#define DEVICE_NAME     "esp32_wroom"
```

### 4. Build and Upload

```bash
# Build
pio run

# Upload to ESP32
pio run --target upload

# Open serial monitor
pio device monitor
```

Or use the PlatformIO toolbar in VSCode:
- ✓ (checkmark) = Build
- → (arrow) = Upload
- 🔌 = Serial Monitor

## Features

### Temperature Reading
- Reads DS18B20 sensor every 10 seconds
- Displays both °C and °F in serial monitor
- Handles sensor errors gracefully

### Local Storage (LittleFS)
- Stores readings to `/temperature_data.csv` on ESP32 flash
- Writes log to `/thermometer.log`
- Data persists across reboots

### Web Integration
- Sends JSON data to `https://wifitemp.jpmac.com` via HTTPS
- Reconnects automatically if WiFi drops

### Time Sync
- Syncs time via NTP on startup
- Sends proper ISO 8601 timestamps

### LED Status Indicator (GPIO2)
```
1 slow blink   = Startup
2 blinks       = WiFi connected
1 blink        = Successful reading sent
3 fast blinks  = Server error
5 fast blinks  = Sensor error
```

## Serial Monitor Output

```
=============================
  WiFi Thermometer - ESP32
=============================
LittleFS mounted
Found 1 sensor(s)
Connecting to WiFi: MyNetwork
....
[2026-02-17T10:00:00] WiFi connected: 192.168.1.101
[2026-02-17T10:00:01] Time synced: 2026-02-17T10:00:01
[2026-02-17T10:00:02] ESP32 Thermometer started
Temperature: 22.56°C / 72.61°F
[2026-02-17T10:00:02] Sent 22.56°C successfully
Temperature: 22.62°C / 72.72°F
[2026-02-17T10:00:12] Sent 22.62°C successfully
```

## Data Format Sent to Server

```json
{
  "temperature": 22.56,
  "unit": "celsius",
  "timestamp": "2026-02-17T10:00:02",
  "device": "esp32_wroom"
}
```

## Accessing Local Data

Using PlatformIO filesystem commands:

```bash
# Download files from ESP32 flash
pio run --target downloadfs

# Files will appear in the project directory under .pio/build/esp32dev/littlefs/
```

## Troubleshooting

### No sensor found
- Check wiring (VDD, GND, DATA)
- Ensure 4.7kΩ resistor is connected between 3.3V and DATA pin
- Check `ONE_WIRE_PIN` in config.h matches your wiring

### WiFi won't connect
- Verify SSID and password in config.h
- ESP32 only supports 2.4GHz WiFi (not 5GHz)
- Check router allows new devices

### HTTPS errors
- ESP32 connects to HTTPS by default
- If you have certificate issues, the HTTPClient will report SSL errors in serial monitor

### LittleFS errors
- Set `LittleFS.begin(true)` to auto-format on first use (already set)
- If persistent issues, use `pio run --target erase` to wipe flash

## Configuration Reference

| Setting | Default | Description |
|---|---|---|
| `ONE_WIRE_PIN` | 4 | GPIO pin for DS18B20 data |
| `READING_INTERVAL_MS` | 10000 | Time between readings (ms) |
| `WIFI_TIMEOUT_MS` | 20000 | WiFi connection timeout (ms) |
| `HTTP_TIMEOUT_MS` | 10000 | HTTP request timeout (ms) |
| `LED_PIN` | 2 | Status LED GPIO (onboard LED) |
| `DEVICE_NAME` | `esp32_wroom` | Identifier shown in dashboard |
