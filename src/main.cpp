// File: src/main.cpp
// ChiperMini_jamPro - Main Entry Point
// Web-controlled WiFi tool for ESP32-S3

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

// ======== GLOBAL STATE ========
AsyncWebServer server(WEB_PORT);
bool attackActive = false;
String targetBSSID = "";
uint8_t targetChannel = 1;
unsigned long attackStart = 0;
unsigned long lastWatchdog = 0;

// ======== FORWARD DECLARATIONS ========
void setupWebServer();
void wifi_scan_start();
void wifi_deauth_start(const String& bssid, uint8_t channel);
void wifi_deauth_stop();
void wifi_deauth_loop();
String getScanResultsJSON();
String getSystemStatusJSON();

// ======== WATCHDOG ========
void feedWatchdog() {
    lastWatchdog = millis();
}

void checkWatchdog() {
    if (millis() - lastWatchdog > WATCHDOG_TIMEOUT_MS) {
        Serial.println("[WATCHDOG] Reset triggered");
        ESP.restart();
    }
}

// ======== SETUP ========
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n[BOOT] " + String(PROJECT_NAME) + " " + String(PROJECT_VERSION));
    Serial.println("[BOOT] Educational purposes only");
    
    // Инициализация WiFi для AP режима
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        Serial.printf("[ERROR] esp_wifi_start failed: %d\n", err);
    }
    WiFi.softAP(AP_SSID, AP_PASS);
    
    Serial.printf("[WiFi] AP: %s | IP: 192.168.4.1\n", AP_SSID);
    
    // Веб-сервер
    setupWebServer();
    server.begin();
    Serial.println("[Web] Server started");
    
    feedWatchdog();
}

// ======== LOOP ========
void loop() {
    feedWatchdog();
    checkWatchdog();
    
    // Деаутер цикл
    if (attackActive) {
        wifi_deauth_loop();
        
        // Авто-стоп
        if (millis() - attackStart > AUTO_STOP_MINUTES * 60000) {
            Serial.println("[AUTO] Attack timeout - stopping");
            wifi_deauth_stop();
        }
    }
    
    delay(10);  // Стабильность
}
