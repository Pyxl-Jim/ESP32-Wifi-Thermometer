#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "config.h"

#ifdef USE_AHT20
#include <Wire.h>
#include <Adafruit_AHTX0.h>
Adafruit_AHTX0 aht;
#else
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);
#endif

// ============================================
// RTC Memory - persists across deep sleep cycles
// ============================================
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool timeSynced = false;

// ============================================
// Sensor Setup
// ============================================
WiFiMulti wifiMulti;

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
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_PIN, LOW);
        delay(delayMs);
    }
}

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
        return "boot-" + String(bootCount);
    }
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return String(buf);
}

// ============================================
// Sensor reading (temp in °C, humidity in %RH or NAN)
// ============================================
struct SensorReading {
    float tempC    = NAN;
    float humidity = NAN;
};

SensorReading readSensors() {
    SensorReading result;

#ifdef USE_AHT20
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

#else
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
#endif

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
        msg += " (boot #" + String(bootCount) + ")";
        logMessage(msg);
        return true;
    } else {
        logMessage("Server error: " + String(responseCode));
        return false;
    }
}

// ============================================
// Go to deep sleep
// ============================================
void goToSleep() {
    logMessage("Sleeping for " + String(READING_INTERVAL_SEC) + "s...");
    Serial.flush();

    // Turn off WiFi and BT to save power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    esp_sleep_enable_timer_wakeup((uint64_t)READING_INTERVAL_SEC * 1000000ULL);
    esp_deep_sleep_start();
}

// ============================================
// Setup - runs on every wake from deep sleep
// ============================================
void setup() {
    Serial.begin(115200);
    delay(500);

    bootCount++;

    pinMode(LED_PIN, OUTPUT);
    ledBlink(1, 200);

    Serial.println("\n=============================");
    Serial.println("  WiFi Thermometer - ESP32");
    Serial.printf("  Wake #%d\n", bootCount);
    Serial.println("=============================");

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }

    // Initialize sensor
#ifdef USE_AHT20
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!aht.begin()) {
        logMessage("ERROR: AHT20 not found! Check wiring (SDA=" + String(I2C_SDA) + " SCL=" + String(I2C_SCL) + ")");
        ledBlink(5, 50);
        goToSleep();
        return;
    }
    logMessage("AHT20 sensor ready");
#else
    sensors.begin();
    if (sensors.getDeviceCount() == 0) {
        logMessage("ERROR: No DS18B20 sensor found!");
        ledBlink(5, 50);
        goToSleep();
        return;
    }
#endif

    // Register WiFi networks
    struct { const char* ssid; const char* pass; } networks[] = WIFI_NETWORKS;
    int networkCount = sizeof(networks) / sizeof(networks[0]);
    for (int i = 0; i < networkCount; i++) {
        wifiMulti.addAP(networks[i].ssid, networks[i].pass);
    }

    // Connect to WiFi
    if (!connectWiFi()) {
        logMessage("No WiFi - storing reading locally only");
        SensorReading reading = readSensors();
        if (!isnan(reading.tempC)) {
            storeReading(getTimestamp(), reading.tempC, reading.humidity);
            logMessage("Stored locally: " + String(reading.tempC, 2) + "°C");
        }
        ledBlink(3, 50);
        goToSleep();
        return;
    }

    // Sync NTP on first boot or every N cycles
    if (!timeSynced || bootCount % NTP_SYNC_INTERVAL_BOOTS == 0) {
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

        if (sendToServer(reading.tempC, reading.humidity, timestamp)) {
            ledBlink(1);
        } else {
            ledBlink(3, 50);
        }
    } else {
        ledBlink(5, 50);
    }

    goToSleep();
}

// ============================================
// Loop - never runs (device sleeps between readings)
// ============================================
void loop() {}
