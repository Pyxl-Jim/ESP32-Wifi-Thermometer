#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "config.h"

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <OneWire.h>
#include <DallasTemperature.h>

Adafruit_AHTX0 aht;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

#ifdef USE_DISPLAY
#include "display_config.h"
static LGFX tft;
#endif

// ============================================
// Sensor reading (temp in °C, humidity in %RH or NAN)
// ============================================
struct SensorReading {
    float tempC    = NAN;
    float humidity = NAN;
};

// ============================================
// State
// ============================================
WiFiMulti wifiMulti;
int loopCount = 0;
bool timeSynced = false;
bool useAHT20 = false;

// ============================================
// Local storage
// ============================================
const char* DATA_FILE = "/temperature_data.csv";
const char* LOG_FILE  = "/thermometer.log";

// ============================================
// Logging
// ============================================
void logMessage(const String& message) {
    String timestamp = "";

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        timestamp = String(buf);
    }

    String logLine = "[" + timestamp + "] " + message;
    Serial.println(logLine);

    File file = LittleFS.open(LOG_FILE, FILE_APPEND);
    if (file) {
        file.println(logLine);
        file.close();
    }
}

// ============================================
// LED helpers
// ============================================
void ledBlink(int times, int delayMs = 100) {
#if LED_PIN < 0
    return;
#else
#ifdef LED_ACTIVE_LOW
    const int ON = LOW, OFF = HIGH;
#else
    const int ON = HIGH, OFF = LOW;
#endif
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, ON);
        delay(delayMs);
        digitalWrite(LED_PIN, OFF);
        delay(delayMs);
    }
#endif
}

// ============================================
// Display
// ============================================
#ifdef USE_DISPLAY
void initDisplay() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(4);
    tft.drawString("WiFi Therm", 86, 130);
    tft.setTextFont(2);
    tft.setTextColor(0x7BEF);
    tft.drawString("Starting...", 86, 165);
}

void updateDisplay(const SensorReading& reading, const String& timestamp, bool wifiOk, bool sendOk) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);

    // Device name
    tft.setTextFont(2);
    tft.setTextColor(0x7BEF);
    tft.drawString(DEVICE_NAME, 86, 8);
    tft.drawFastHLine(10, 28, 152, 0x2945);

    if (isnan(reading.tempC)) {
        tft.setTextColor(TFT_RED);
        tft.setTextFont(4);
        tft.drawString("No Sensor", 86, 145);
        return;
    }

    // Temperature °F (large)
    char buf[16];
    float tempF = reading.tempC * 9.0f / 5.0f + 32.0f;
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(6);  // 48px digits
    snprintf(buf, sizeof(buf), "%.1f", tempF);
    tft.drawString(buf, 86, 38);

    tft.setTextColor(0xAD75);
    tft.setTextFont(4);
    tft.drawString("F", 86, 96);

    // Celsius
    tft.setTextFont(2);
    tft.setTextColor(0x7BEF);
    snprintf(buf, sizeof(buf), "%.2f C", reading.tempC);
    tft.drawString(buf, 86, 126);

    tft.drawFastHLine(10, 148, 152, 0x2945);

    // Humidity (DS18B20 won't have this, but show if present)
    if (!isnan(reading.humidity)) {
        tft.setTextColor(TFT_CYAN);
        tft.setTextFont(4);
        snprintf(buf, sizeof(buf), "%.1f%%", reading.humidity);
        tft.drawString(buf, 86, 158);
        tft.setTextFont(2);
        tft.setTextColor(0x7BEF);
        tft.drawString("Humidity", 86, 190);
        tft.drawFastHLine(10, 210, 152, 0x2945);
    }

    // WiFi / send status
    uint16_t statusY = isnan(reading.humidity) ? 165 : 220;
    tft.setTextFont(2);
    if (!wifiOk) {
        tft.setTextColor(TFT_RED);
        tft.drawString("No WiFi - stored locally", 86, statusY);
    } else if (!sendOk) {
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Send failed", 86, statusY);
    } else {
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Sent OK", 86, statusY);
    }

    // Time of last reading (HH:MM:SS portion)
    tft.setTextColor(0x4A49);
    if (timestamp.length() >= 19) {
        tft.drawString(timestamp.substring(11, 19), 86, statusY + 22);
    }

    // Reading count
    snprintf(buf, sizeof(buf), "Reading #%d", loopCount);
    tft.drawString(buf, 86, statusY + 44);
}
#endif

// ============================================
// WiFi
// ============================================
bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    logMessage("Connecting to WiFi...");

    unsigned long startTime = millis();
    while (wifiMulti.run() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_TIMEOUT_MS) {
            logMessage("WiFi connection timed out");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    logMessage("WiFi connected to: " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")");
    ledBlink(2);
    return true;
}

// ============================================
// NTP Time Sync
// ============================================
void syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo) && retries < 10) {
        delay(500);
        retries++;
    }

    if (retries < 10) {
        timeSynced = true;
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        logMessage("Time synced: " + String(buf));
    } else {
        logMessage("NTP sync failed");
    }
}

// ============================================
// Get ISO 8601 timestamp
// ============================================
String getTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "loop-" + String(loopCount);
    }
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return String(buf);
}

SensorReading readSensors() {
    SensorReading result;

    if (useAHT20) {
        sensors_event_t humEvent, tempEvent;
        if (!aht.getEvent(&humEvent, &tempEvent)) {
            logMessage("Sensor error: AHT20 read failed");
            return result;
        }

        float t = tempEvent.temperature;
        float h = humEvent.relative_humidity;

        if (t < -40.0 || t > 85.0) {
            logMessage("Sensor error: temperature out of range: " + String(t));
            return result;
        }
        if (h < 0.0 || h > 100.0) {
            logMessage("Sensor error: humidity out of range: " + String(h));
            return result;
        }

        result.tempC    = t;
        result.humidity = h;
    } else {
        // Discard first read - DS18B20 returns 85°C (power-on default) on first conversion
        sensors.requestTemperatures();
        delay(800);
        sensors.requestTemperatures();
        float temp = sensors.getTempCByIndex(0);

        if (temp == DEVICE_DISCONNECTED_C) {
            logMessage("Sensor error: device disconnected");
            return result;
        }
        if (temp < -55.0 || temp > 125.0) {
            logMessage("Sensor error: reading out of range: " + String(temp));
            return result;
        }

        result.tempC = temp;
    }

    return result;
}

// ============================================
// Local CSV Storage
// ============================================
void storeReading(const String& timestamp, float tempC, float humidity) {
    bool fileExists = LittleFS.exists(DATA_FILE);

    File file = LittleFS.open(DATA_FILE, FILE_APPEND);
    if (!file) {
        logMessage("Failed to open data file for writing");
        return;
    }

    if (!fileExists) {
        file.println("timestamp,temperature_celsius,humidity_rh");
    }

    String humStr = isnan(humidity) ? "" : String(humidity, 1);
    file.println(timestamp + "," + String(tempC, 2) + "," + humStr);
    file.close();
}

// ============================================
// Send to Web Server
// ============================================
bool sendToServer(float tempC, float humidity, const String& timestamp) {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    JsonDocument doc;
    doc["temperature"] = tempC;
    doc["unit"]        = "celsius";
    doc["timestamp"]   = timestamp;
    doc["device"]      = DEVICE_NAME;
    if (!isnan(humidity)) {
        doc["humidity"] = humidity;
    }

    String payload;
    serializeJson(doc, payload);

    int responseCode = http.POST(payload);
    http.end();

    if (responseCode == 200) {
        String msg = "Sent " + String(tempC, 2) + "°C";
        if (!isnan(humidity)) msg += " / " + String(humidity, 1) + "% RH";
        msg += " (reading #" + String(loopCount) + ")";
        logMessage(msg);
        return true;
    } else {
        logMessage("Server error: " + String(responseCode));
        return false;
    }
}

// ============================================
// Setup - runs once on power-on
// ============================================
void setup() {
    Serial.begin(115200);
    delay(500);

#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
    ledBlink(1, 200);
#endif

#ifdef USE_DISPLAY
    initDisplay();
#endif

    Serial.println("\n=============================");
    Serial.println("  WiFi Thermometer - ESP32");
    Serial.println("=============================");

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }

    // Initialize sensors - try AHT20 first, fall back to DS18B20
    Wire.begin(I2C_SDA, I2C_SCL);
    if (aht.begin()) {
        logMessage("AHT20 sensor ready (SDA=" + String(I2C_SDA) + " SCL=" + String(I2C_SCL) + ")");
        useAHT20 = true;
    } else {
        logMessage("AHT20 not found, trying DS18B20 on pin " + String(ONE_WIRE_PIN) + "...");
        sensors.begin();
        if (sensors.getDeviceCount() > 0) {
            logMessage("DS18B20 sensor ready");
        } else {
            logMessage("ERROR: No sensor found!");
            ledBlink(5, 50);
        }
    }

    // Register WiFi networks
    struct { const char* ssid; const char* pass; } networks[] = WIFI_NETWORKS;
    int networkCount = sizeof(networks) / sizeof(networks[0]);
    for (int i = 0; i < networkCount; i++) {
        wifiMulti.addAP(networks[i].ssid, networks[i].pass);
    }

    // Connect to WiFi and sync time
    if (connectWiFi()) {
        syncTime();
    }
}

// ============================================
// Loop - reads and transmits every READING_INTERVAL_SEC
// ============================================
void loop() {
    loopCount++;

    // Reconnect WiFi if dropped
    if (!connectWiFi()) {
        logMessage("No WiFi - storing reading locally only");
        SensorReading reading = readSensors();
        if (!isnan(reading.tempC)) {
            String ts = getTimestamp();
            storeReading(ts, reading.tempC, reading.humidity);
            logMessage("Stored locally: " + String(reading.tempC, 2) + "°C");
#ifdef USE_DISPLAY
            updateDisplay(reading, ts, false, false);
#endif
        }
        ledBlink(3, 50);
        delay(READING_INTERVAL_SEC * 1000);
        return;
    }

    // Periodically re-sync NTP
    if (!timeSynced || loopCount % NTP_SYNC_INTERVAL == 0) {
        syncTime();
    }

    // Read sensors
    SensorReading reading = readSensors();

    if (!isnan(reading.tempC)) {
        String timestamp = getTimestamp();

        Serial.printf("Temperature: %.2f°C / %.2f°F\n",
            reading.tempC, (reading.tempC * 9.0 / 5.0) + 32.0);
        if (!isnan(reading.humidity)) {
            Serial.printf("Humidity:    %.1f%%\n", reading.humidity);
        }

        storeReading(timestamp, reading.tempC, reading.humidity);

        bool sent = sendToServer(reading.tempC, reading.humidity, timestamp);
#ifdef USE_DISPLAY
        updateDisplay(reading, timestamp, true, sent);
#endif
        if (sent) {
            ledBlink(1);
        } else {
            ledBlink(3, 50);
        }
    } else {
#ifdef USE_DISPLAY
        SensorReading empty;
        updateDisplay(empty, getTimestamp(), true, false);
#endif
        ledBlink(5, 50);
    }

    delay(READING_INTERVAL_SEC * 1000);
}
