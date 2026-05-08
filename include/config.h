#pragma once

#define PROJECT_NAME        "ChiperMini_jamPro"
#define PROJECT_VERSION     "v1.0 BETA"
#define AP_SSID             "ChiperMini_jamPro"
#define AP_PASS             "1234567890"
#define WEB_PORT            80

// ======== WiFi Deauther Settings ========
#define DEAUTH_INTERVAL_MS      50          // Интервал между пакетами (мс)
#define DEAUTH_REASON           0x0007      // Reason: Class3 frame from non-assoc STA
#define MAX_SCAN_RESULTS        20          // Макс. сетей в скане
#define AUTO_STOP_MINUTES       10          // Авто-стоп атаки через Х минут

// ======== MAC Rotation ========
#define MAC_ROTATION_INTERVAL_MS 3000       // Менять MAC каждые 3 секунды
#define PACKETS_PER_MAC         15          // Или каждые 15 пакетов

// ======== Safety & Stability ========
#define WATCHDOG_TIMEOUT_MS     10000       // Перезагрузка если завис >10 сек
#define MAC_CHANGE_DELAY_MS     2           // Задержка после смены MAC (мс)
#define TX_DELAY_US             250         // Задержка после отправки пакета (мкс)

// ======== WiFi ========
#define WIFI_CHANNEL_AUTO       true        // Авто-переключение на канал цели
