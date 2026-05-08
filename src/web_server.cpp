// File: src/web_server.cpp
// ChiperMini_jamPro - Web Interface
// FIXED: Forward declarations for JSON helpers

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

// ======== EXTERNAL VARIABLES ========
extern AsyncWebServer server;
extern bool attackActive;
extern String targetBSSID;
extern uint8_t targetChannel;
extern unsigned long attackStart;

// ======== FORWARD DECLARATIONS (FIXES COMPILATION) ========
String getScanResultsJSON();
String getSystemStatusJSON();

// ======== HTML INTERFACE ========
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ChiperMini_jamPro</title>
<style>
body{font-family:monospace;background:#0a0a0a;color:#0f0;padding:20px;margin:0}
.container{max-width:600px;margin:0 auto}
h1{color:#0ff;border-bottom:1px solid #333;padding-bottom:10px}
.card{background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:15px;margin:10px 0}
.btn{background:#0f0;color:#000;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;font-weight:bold}
.btn:disabled{background:#333;color:#666;cursor:not-allowed}
.btn-red{background:#f44}
.network{padding:8px;border-bottom:1px solid #222;display:flex;justify-content:space-between;align-items:center}
.network:last-child{border-bottom:none}
.status{color:#0ff;font-size:0.9em}
.log{background:#111;padding:10px;border-radius:4px;max-height:200px;overflow-y:auto;font-size:0.85em}
.channel-badge{background:#333;padding:2px 6px;border-radius:3px;font-size:0.8em}
</style>
</head><body>
<div class="container">
<h1>🛡️ ChiperMini_jamPro</h1>

<div class="card">
<h3>📡 WiFi Scan</h3>
<button class="btn" onclick="scan()">Scan Networks</button><div id="scanResults" style="margin-top:10px"></div>
</div>

<div class="card">
<h3>⚡ Deauth Attack</h3>
<p class="status">Target: <span id="target">None</span></p>
<p class="status">Channel: <span id="channel">-</span></p>
<p class="status">Status: <span id="attackStatus">Idle</span></p>
<button class="btn btn-red" id="startBtn" onclick="startAttack()" disabled>Start Attack</button>
<button class="btn" id="stopBtn" onclick="stopAttack()" disabled>Stop</button>
</div>

<div class="card">
<h3>📋 System</h3>
<p>Version: <span id="version">-</span></p>
<p>Uptime: <span id="uptime">-</span></p>
<p>MAC: <span id="mac">-</span></p>
<button class="btn" onclick="refreshStatus()">Refresh</button>
</div>

<div class="card">
<h3>🔍 Log</h3>
<div class="log" id="log"></div>
</div>

<p style="font-size:0.7em;color:#555;text-align:center;margin-top:20px">
Educational use only | <span id="project"></span>
</p>
</div>

<script>
const API = 'http://192.168.4.1';
async function api(endpoint) {
    try { const r = await fetch(API + endpoint); return await r.json(); }
    catch(e) { log('API error: ' + e); return null; }
}
function log(msg) {
    const el = document.getElementById('log');
    const time = new Date().toLocaleTimeString();
    el.innerHTML = `[${time}] ${msg}<br>` + el.innerHTML;
    if (el.children.length > 50) el.lastChild.remove();
}
async function scan() {
    log('Scanning...');
    document.getElementById('scanResults').innerHTML = 'Scanning...';
    const data = await api('/scan');
    if (!data) return;
    let html = '';
    data.networks.forEach((n, i) => {
        html += `<div class="network">            <span>${n.ssid || '(hidden)'} <small style="color:#666">[${n.bssid}]</small></span>
            <span><span class="channel-badge">CH${n.channel}</span> ${n.rssi}dBm <button onclick="selectTarget('${n.bssid}',${n.channel})" style="margin-left:5px">→</button></span>
        </div>`;
    });
    document.getElementById('scanResults').innerHTML = html;
    log(`Found ${data.networks.length} networks`);
}
function selectTarget(bssid, channel) {
    targetBSSID = bssid; targetChannel = channel;
    document.getElementById('target').textContent = bssid;
    document.getElementById('channel').textContent = channel;
    document.getElementById('startBtn').disabled = false;
    log(`Target selected: ${bssid} on channel ${channel}`);
}
async function startAttack() {
    if (!targetBSSID) return;
    log('Starting attack...');
    const r = await api('/attack/start?bssid=' + targetBSSID + '&channel=' + targetChannel);
    if (r?.success) {
        document.getElementById('attackStatus').textContent = 'ACTIVE';
        document.getElementById('attackStatus').style.color = '#f44';
        document.getElementById('startBtn').disabled = true;
        document.getElementById('stopBtn').disabled = false;
        log('Attack started with MAC rotation every 3s');
    }
}
async function stopAttack() {
    log('Stopping attack...');
    const r = await api('/attack/stop');
    if (r?.success) {
        document.getElementById('attackStatus').textContent = 'Idle';
        document.getElementById('attackStatus').style.color = '#0f0';
        document.getElementById('startBtn').disabled = !targetBSSID;
        document.getElementById('stopBtn').disabled = true;
        log('Attack stopped');
    }
}
async function refreshStatus() {
    const data = await api('/status');
    if (!data) return;
    document.getElementById('version').textContent = data.version;
    document.getElementById('uptime').textContent = Math.floor(data.uptime / 1000) + 's';
    document.getElementById('mac').textContent = data.mac;
    document.getElementById('project').textContent = data.project;
    document.getElementById('attackStatus').textContent = data.attack_active ? 'ACTIVE' : 'Idle';
    document.getElementById('attackStatus').style.color = data.attack_active ? '#f44' : '#0f0';
}
setInterval(refreshStatus, 5000);
refreshStatus();
log('Interface loaded');</script>
</body></html>
)rawliteral";

// ======== JSON HELPERS (DEFINED BEFORE setupWebServer) ========

String getScanResultsJSON() {
    String json = "{\"networks\":[";
    int n = WiFi.scanNetworks();
    for (int i = 0; i < min(n, MAX_SCAN_RESULTS); i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"channel\":" + String(WiFi.channel(i));
        json += "}";
    }
    json += "],\"count\":" + String(min(n, MAX_SCAN_RESULTS)) + "}";
    WiFi.scanDelete();
    return json;
}

String getSystemStatusJSON() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    String json = "{";
    json += "\"project\":\"" + String(PROJECT_NAME) + "\",";
    json += "\"version\":\"" + String(PROJECT_VERSION) + "\",";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"mac\":\"";
    for (int i = 0; i < 6; i++) {
        json += String(mac[i], HEX);
        if (i < 5) json += ":";
    }
    json += "\",";
    json += "\"attack_active\":" + String(attackActive ? "true" : "false");
    json += "}";
    return json;
}

// ======== WEB ROUTES ========
void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", INDEX_HTML);
    });
    
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", getScanResultsJSON());
    });    
    server.on("/attack/start", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("bssid") && request->hasParam("channel")) {
            String bssid = request->getParam("bssid")->value();
            uint8_t channel = request->getParam("channel")->value().toInt();
            wifi_deauth_start(bssid, channel);
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing bssid or channel\"}");
        }
    });
    
    server.on("/attack/stop", HTTP_GET, [](AsyncWebServerRequest *request){
        wifi_deauth_stop();
        request->send(200, "application/json", "{\"success\":true}");
    });
    
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", getSystemStatusJSON());  // ✅ Теперь видит функцию
    });
    
    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });
}
