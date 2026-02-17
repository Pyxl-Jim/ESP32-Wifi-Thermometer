#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "config.h"

WiFiMulti wifiMulti;

// ============================================
// Sensor Setup
// ============================================
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

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

    // Get time if NTP is synced
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        timestamp = String(buf);
    }

    String logLine = "[" + timestamp + "] " + message;
    Serial.println(logLine);

    // Write to log file
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

    logMessage("Scanning for known networks...");

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
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        logMessage("Time synced: " + String(buf));
    } else {
        logMessage("NTP sync failed - timestamps may be incorrect");
    }
}

// ============================================
// Get ISO 8601 timestamp
// ============================================
String getTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return String(millis());  // Fallback to uptime ms
    }
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return String(buf);
}

// ============================================
// Temperature Sensor
// ============================================
float readTemperature() {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);

    if (temp == DEVICE_DISCONNECTED_C) {
        logMessage("Sensor error: device disconnected");
        return NAN;
    }

    if (temp < -55.0 || temp > 125.0) {
        logMessage("Sensor error: reading out of range: " + String(temp));
        return NAN;
    }

    return temp;
}

// ============================================
// Local CSV Storage
// ============================================
void storeReading(const String& timestamp, float tempC) {
    bool fileExists = LittleFS.exists(DATA_FILE);

    File file = LittleFS.open(DATA_FILE, FILE_APPEND);
    if (!file) {
        logMessage("Failed to open data file for writing");
        return;
    }

    // Write header if new file
    if (!fileExists) {
        file.println("timestamp,temperature_celsius");
    }

    file.println(timestamp + "," + String(tempC, 2));
    file.close();
}

// ============================================
// Send to Web Server
// ============================================
bool sendToServer(float tempC, const String& timestamp) {
    if (WiFi.status() != WL_CONNECTED) {
        if (!connectWiFi()) {
            logMessage("Cannot send - no WiFi");
            return false;
        }
    }

    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    // Build JSON payload
    JsonDocument doc;
    doc["temperature"] = tempC;
    doc["unit"]        = "celsius";
    doc["timestamp"]   = timestamp;
    doc["device"]      = DEVICE_NAME;

    String payload;
    serializeJson(doc, payload);

    int responseCode = http.POST(payload);

    if (responseCode == 200) {
        logMessage("Sent " + String(tempC, 2) + "°C successfully");
        http.end();
        return true;
    } else {
        logMessage("Server error: " + String(responseCode));
        http.end();
        return false;
    }
}

// ============================================
// Setup
// ============================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);
    ledBlink(1, 500);

    Serial.println("\n=============================");
    Serial.println("  WiFi Thermometer - ESP32");
    Serial.println("=============================");

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed - reformatting...");
        LittleFS.format();
        LittleFS.begin();
    }
    Serial.println("LittleFS mounted");

    // Initialize sensor
    sensors.begin();
    int deviceCount = sensors.getDeviceCount();
    if (deviceCount == 0) {
        Serial.println("WARNING: No DS18B20 sensors found!");
        Serial.println("Check wiring and pull-up resistor.");
    } else {
        Serial.println("Found " + String(deviceCount) + " sensor(s)");
    }

    // Register all WiFi networks
    struct { const char* ssid; const char* pass; } networks[] = WIFI_NETWORKS;
    int networkCount = sizeof(networks) / sizeof(networks[0]);
    for (int i = 0; i < networkCount; i++) {
        wifiMulti.addAP(networks[i].ssid, networks[i].pass);
        logMessage("Added network: " + String(networks[i].ssid));
    }

    // Connect to WiFi
    if (connectWiFi()) {
        syncTime();
    }

    logMessage("ESP32 Thermometer started");
    logMessage("Sensor pin: GPIO" + String(ONE_WIRE_PIN));
    logMessage("Server: " + String(SERVER_URL));
    logMessage("Interval: " + String(READING_INTERVAL_MS / 1000) + " seconds");
}

// ============================================
// Main Loop
// ============================================
void loop() {
    unsigned long loopStart = millis();

    // Read temperature
    float tempC = readTemperature();

    if (!isnan(tempC)) {
        String timestamp = getTimestamp();

        // Log to serial
        Serial.println("Temperature: " + String(tempC, 2) + "°C / " +
                       String((tempC * 9.0 / 5.0) + 32.0, 2) + "°F");

        // Store locally
        storeReading(timestamp, tempC);

        // Send to server
        if (sendToServer(tempC, timestamp)) {
            ledBlink(1);  // Success blink
        } else {
            ledBlink(3, 50);  // Error blink
        }
    } else {
        ledBlink(5, 50);  // Sensor error blink
    }

    // Wait for next reading (accounting for execution time)
    unsigned long elapsed = millis() - loopStart;
    if (elapsed < READING_INTERVAL_MS) {
        delay(READING_INTERVAL_MS - elapsed);
    }
}
