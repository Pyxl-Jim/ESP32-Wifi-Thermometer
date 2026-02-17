#pragma once

// ============================================
// WiFi Credentials
// Add networks to include/secrets.h (gitignored)
// Copy include/secrets.h.example to get started
// ============================================
#include "secrets.h"

// ============================================
// Server Configuration
// ============================================
#define SERVER_URL      "https://wifitemp.jpmac.com"
#define DEVICE_NAME     "esp32_wroom"

// ============================================
// Sensor Configuration
// ============================================
// GPIO pin connected to DS18B20 data line
// Requires 4.7kΩ pull-up resistor to 3.3V
#define ONE_WIRE_PIN    4

// ============================================
// Timing Configuration
// ============================================
#define READING_INTERVAL_MS     10000   // 10 seconds
#define WIFI_TIMEOUT_MS         20000   // 20 seconds to connect
#define HTTP_TIMEOUT_MS         10000   // 10 seconds for HTTP request

// ============================================
// Status LED (GPIO2 = onboard LED on most ESP32 boards)
// ============================================
#define LED_PIN         2
