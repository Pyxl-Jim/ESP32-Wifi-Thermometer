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

// DEVICE_NAME must be defined in secrets.h
// e.g. #define DEVICE_NAME "Greenhouse 2"
#ifndef DEVICE_NAME
#error "DEVICE_NAME must be defined in secrets.h"
#endif

// ============================================
// Sensor Configuration
// ============================================
// DS18B20 (default sensor, used on WROOM)
// GPIO pin connected to DS18B20 data line
// Requires 4.7kΩ pull-up resistor to 3.3V
#ifndef ONE_WIRE_PIN
#define ONE_WIRE_PIN    4
#endif

// AHT20 I2C sensor (used on ESP32-C3 with -DUSE_AHT20)
// Default pins - override via build_flags: -DI2C_SDA=5 -DI2C_SCL=6
#ifndef I2C_SDA
#define I2C_SDA         21
#endif
#ifndef I2C_SCL
#define I2C_SCL         22
#endif

// ============================================
// Timing Configuration
// ============================================
#define READING_INTERVAL_SEC    60      // seconds between readings (deep sleep duration)
#define WIFI_TIMEOUT_MS         20000   // 20 seconds to connect
#define HTTP_TIMEOUT_MS         10000   // 10 seconds for HTTP request

// NTP re-sync interval - no need to sync every wake cycle
#define NTP_SYNC_INTERVAL_BOOTS 20      // Re-sync NTP every N wake cycles

// ============================================
// Status LED
// GPIO2 = onboard LED on ESP32 WROOM
// GPIO8 = onboard LED on ESP32-C3
// ============================================
#ifndef LED_PIN
#define LED_PIN         2
#endif
