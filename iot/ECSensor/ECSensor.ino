/**
 * ============================================================
 *  ESP32 + TCS34725 Color Sensor — IoT Data Sender
 *  Version : 4.0
 *  Features:
 *    - WiFiManager dengan halaman konfigurasi custom HTML (premium UI)
 *    - Token API tersimpan di LittleFS (persisten)
 *    - Kirim data ke Django REST API via HTTP POST
 *    - Auto-reconnect dengan exponential backoff
 *    - Jika gagal reconnect 3x → buka portal WiFiManager lagi
 *    - mDNS: akses portal via http://esp32sensor.local
 *    - Tahan tombol BOOT (GPIO 0) untuk reset WiFi + Token
 *    - [NEW v4.0] Local HTTP Server port 80 untuk device discovery
 *    - [NEW v4.0] Heartbeat ke Django setiap 15 detik (auto-register)
 *    - [NEW v4.0] CORS header agar browser bisa direct fetch
 * ============================================================
 *
 *  Library yang dibutuhkan (Install via Library Manager):
 *    1. WiFiManager  by tzapu     https://github.com/tzapu/WiFiManager
 *    2. ArduinoJson  by bblanchon https://github.com/bblanchon/ArduinoJson
 *    3. Adafruit TCS34725         https://github.com/adafruit/Adafruit_TCS34725
 *    4. Adafruit BusIO            (dependensi otomatis)
 *    5. LittleFS                  (built-in ESP32 Arduino Core)
 *    6. ESPmDNS                   (built-in ESP32 Arduino Core)
 *    7. WebServer                 (built-in ESP32 Arduino Core)  ← NEW
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "Adafruit_TCS34725.h"

// ============================================================
//  KONFIGURASI — Sesuaikan bagian ini
// ============================================================

const char* AP_SSID      = "ESP32_Sensor_Color";
const char* AP_PASSWORD  = "Admin1234";
const char* MDNS_NAME    = "esp32sensor";
const char* SERVER_URL   = "https://ozora.b4its.tech/api/receive-data/";
const char* HEARTBEAT_URL = "https://ozora.b4its.tech/api/device/heartbeat/";

#define TRIGGER_PIN             0
#define SEND_INTERVAL           10000
#define HEARTBEAT_INTERVAL      15000
#define MAX_RECONNECT_ATTEMPTS  3
#define RECONNECT_BASE_DELAY    3000
#define CONFIG_FILE             "/config.json"

// ============================================================
//  VARIABEL GLOBAL
// ============================================================

char apiToken[150]        = "";
bool shouldSaveConfig     = false;
int  reconnectAttempts    = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastSensor    = 0;

// Local HTTP server untuk device discovery (port 80)
WebServer localServer(80);

Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);

// ============================================================
//  CUSTOM HTML PORTAL WIFIMANAGER  (Premium Dark UI v3)
// ============================================================

const char CUSTOM_HEAD[] PROGMEM = R"rawliteral(
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap');

  :root {
    --bg:        #060912;
    --surface:   #0d1117;
    --card:      #161b27;
    --border:    #21293d;
    --border2:   #2d3a52;
    --text:      #e6edf3;
    --muted:     #8b949e;
    --accent:    #4f8ef7;
    --accent2:   #6366f1;
    --success:   #3fb950;
    --warning:   #d29922;
    --glow:      rgba(79,142,247,0.18);
  }

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    font-family: 'Inter', system-ui, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 20px 16px 40px;
    background-image: radial-gradient(ellipse 80% 50% at 50% -20%, rgba(79,142,247,.08) 0%, transparent 70%);
  }

  .top-nav {
    width: 100%; max-width: 460px;
    display: flex; align-items: center; justify-content: space-between;
    margin-bottom: 28px;
  }
  .brand { display: flex; align-items: center; gap: 10px; }
  .brand-icon {
    width: 36px; height: 36px; border-radius: 10px;
    background: linear-gradient(135deg, var(--accent), var(--accent2));
    display: flex; align-items: center; justify-content: center;
    font-size: 18px; box-shadow: 0 4px 14px var(--glow);
  }
  .brand-name { font-size: 15px; font-weight: 700; color: var(--text); }
  .brand-sub  { font-size: 11px; color: var(--muted); }
  .status-pill {
    display: flex; align-items: center; gap: 7px;
    background: rgba(63,185,80,.1); border: 1px solid rgba(63,185,80,.25);
    border-radius: 99px; padding: 5px 12px;
    font-size: 12px; font-weight: 500; color: var(--success);
  }
  .pulse-dot {
    width: 7px; height: 7px; border-radius: 50%;
    background: var(--success);
    animation: pulse 1.8s ease-in-out infinite;
  }
  @keyframes pulse { 0%,100%{opacity:1;transform:scale(1);}50%{opacity:.4;transform:scale(1.3);} }

  .card {
    width: 100%; max-width: 460px;
    background: var(--card); border: 1px solid var(--border);
    border-radius: 18px; padding: 28px 26px;
    box-shadow: 0 0 0 1px rgba(255,255,255,.03), 0 24px 64px rgba(0,0,0,.55);
    position: relative; overflow: hidden;
  }
  .card::before {
    content: '';
    position: absolute; top: 0; left: 0; right: 0; height: 1px;
    background: linear-gradient(90deg, transparent, rgba(79,142,247,.4), transparent);
  }
  .section-title { font-size: 18px; font-weight: 700; letter-spacing: -0.4px; margin-bottom: 3px; }
  .section-sub { font-size: 13px; color: var(--muted); margin-bottom: 22px; line-height: 1.5; }
  .chips { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 22px; }
  .chip {
    display: inline-flex; align-items: center; gap: 5px;
    background: rgba(255,255,255,.04); border: 1px solid var(--border);
    border-radius: 99px; padding: 4px 11px;
    font-size: 11px; color: var(--muted); font-weight: 500;
  }
  .chip.blue { background: rgba(79,142,247,.08); border-color: rgba(79,142,247,.25); color: #93b4f5; }
  .divider { display: flex; align-items: center; gap: 12px; margin: 22px 0; }
  .divider hr { flex: 1; border: none; border-top: 1px solid var(--border); }
  .divider span { font-size: 11px; color: var(--muted); white-space: nowrap; font-weight: 500; }
  .field-group { margin-bottom: 18px; }
  label {
    display: flex; align-items: center; gap: 6px;
    font-size: 11px; font-weight: 600; text-transform: uppercase;
    letter-spacing: .7px; color: var(--muted); margin-bottom: 7px;
  }
  input[type=text], input[type=password] {
    width: 100%; background: var(--surface); border: 1px solid var(--border2);
    border-radius: 11px; padding: 11px 14px; font-size: 14px; color: var(--text);
    outline: none; transition: border-color .2s, box-shadow .2s; font-family: inherit;
  }
  input[type=text]:focus, input[type=password]:focus {
    border-color: var(--accent); box-shadow: 0 0 0 3px rgba(79,142,247,.15);
  }
  input[type=text]::placeholder, input[type=password]::placeholder { color: #3d4f69; }
  .token-wrap { position: relative; }
  .token-wrap input { padding-right: 42px; font-family: 'JetBrains Mono', 'Courier New', monospace; font-size: 13px; }
  .token-eye {
    position: absolute; right: 13px; top: 50%; transform: translateY(-50%);
    cursor: pointer; color: var(--muted); font-size: 16px;
    background: none; border: none; padding: 2px; transition: color .15s;
    width: auto !important; margin: 0 !important;
  }
  .token-eye:hover { color: var(--accent); background: none; }
  .hint-box {
    background: rgba(79,142,247,.06); border: 1px solid rgba(79,142,247,.2);
    border-left: 3px solid var(--accent); border-radius: 10px;
    padding: 12px 14px; font-size: 12px; color: #7aa3e8; margin-bottom: 20px; line-height: 1.65;
  }
  .hint-box strong { color: #a5c0f0; display: block; margin-bottom: 4px; font-weight: 600; }
  .hint-box code {
    background: rgba(79,142,247,.15); border-radius: 5px; padding: 2px 6px;
    font-family: 'JetBrains Mono', monospace; font-size: 11.5px; color: #bdd4f8;
  }
  .copy-btn {
    display: inline-flex; align-items: center; gap: 4px;
    background: rgba(79,142,247,.12); border: 1px solid rgba(79,142,247,.25);
    color: var(--accent); border-radius: 6px; padding: 3px 9px;
    font-size: 11px; font-weight: 600; cursor: pointer; margin-top: 7px;
    width: auto !important; transition: background .15s;
  }
  .copy-btn:hover { background: rgba(79,142,247,.22); }
  input[type=submit] {
    width: 100%; background: linear-gradient(135deg, var(--accent) 0%, var(--accent2) 100%);
    border: none; border-radius: 11px; padding: 13px; font-size: 15px; font-weight: 600;
    color: #fff; cursor: pointer; transition: opacity .2s, transform .1s, box-shadow .2s;
    box-shadow: 0 4px 18px rgba(79,142,247,.3); margin-top: 6px;
  }
  input[type=submit]:hover  { opacity: .88; }
  input[type=submit]:active { transform: scale(.98); }
  button {
    width: 100%; background: var(--surface); border: 1px solid var(--border2);
    border-radius: 11px; padding: 11px; font-size: 14px; font-weight: 500;
    color: var(--muted); cursor: pointer; transition: border-color .2s, color .2s; margin-top: 4px;
  }
  button:hover { border-color: var(--accent); color: var(--text); }
  .access-info {
    background: rgba(63,185,80,.06); border: 1px solid rgba(63,185,80,.2);
    border-radius: 11px; padding: 12px 14px; margin-top: 18px;
    font-size: 12px; color: #6dba83; line-height: 1.6;
  }
  .access-info strong { color: #8dd4a0; display: block; margin-bottom: 4px; }
  .access-info .url-badge {
    display: inline-block; background: rgba(63,185,80,.1); border: 1px solid rgba(63,185,80,.2);
    border-radius: 6px; padding: 2px 8px; font-family: monospace; font-size: 12px; color: #a8e6b8;
  }
  .footer { margin-top: 22px; font-size: 11px; color: #2d3a52; text-align: center; line-height: 1.8; }
  .footer a { color: #3d5475; text-decoration: none; }
  .footer a:hover { color: var(--accent); }
  @media (max-width: 480px) { .card { padding: 22px 18px; border-radius: 14px; } }
</style>
)rawliteral";

const char CUSTOM_BODY[] PROGMEM = R"rawliteral(
<div class="top-nav">
  <div class="brand">
    <div class="brand-icon">🎨</div>
    <div>
      <div class="brand-name">Color Sensor</div>
      <div class="brand-sub">ESP32 IoT Device v4.0</div>
    </div>
  </div>
  <div class="status-pill"><span class="pulse-dot"></span>AP Mode</div>
</div>

<div class="card">
  <div class="section-title">Konfigurasi Perangkat</div>
  <p class="section-sub">Hubungkan ke jaringan WiFi dan masukkan API Token untuk mulai mengirim data sensor.</p>
  <div class="chips">
    <span class="chip">📡 TCS34725 RGB Sensor</span>
    <span class="chip blue">⚡ ESP32</span>
    <span class="chip">🔄 v4.0</span>
  </div>
  <div class="hint-box">
    <strong>🔑 API Token</strong>
    Paste token hash dari Dashboard. Prefix <code>Token </code> akan ditambah otomatis.<br><br>
    Contoh: <code>901f60a25e09226af5dbb003859ef817...</code>
    <br><button class="copy-btn" onclick="copyFormat()">📋 Salin Format</button>
  </div>
  {v}
  <div class="access-info">
    <strong>🌐 Akses Portal Kapan Saja</strong>
    Setelah terhubung WiFi, buka portal via:<br>
    <span class="url-badge">http://esp32sensor.local</span>
    &nbsp;atau IP: <span class="url-badge" id="localip">192.168.4.1</span>
  </div>
</div>

<div class="footer">
  ESP32 IoT Color Sensor &nbsp;·&nbsp; v4.0<br>
  Perangkat akan restart otomatis setelah konfigurasi tersimpan.<br>
  <a href="/wifi">Scan WiFi</a> &nbsp;·&nbsp; <a href="/i">Info</a> &nbsp;·&nbsp; <a href="/r">Restart</a>
</div>

<script>
  function toggleToken() {
    var inp = document.getElementById('token');
    var btn = document.getElementById('eyeBtn');
    if (!inp) return;
    inp.type = inp.type === 'password' ? 'text' : 'password';
    btn.textContent = inp.type === 'password' ? '👁' : '🙈';
  }
  window.addEventListener('DOMContentLoaded', function() {
    var tokenInp = document.getElementById('token') || document.querySelector('input[name=token]');
    if (tokenInp) {
      tokenInp.type = 'password';
      tokenInp.placeholder = 'Token xxxxxxxxxxxxxxxxxxxxxxxx';
      var wrap = document.createElement('div');
      wrap.className = 'token-wrap';
      tokenInp.parentNode.insertBefore(wrap, tokenInp);
      wrap.appendChild(tokenInp);
      var eye = document.createElement('button');
      eye.id = 'eyeBtn'; eye.className = 'token-eye'; eye.type = 'button';
      eye.textContent = '👁'; eye.onclick = toggleToken;
      wrap.appendChild(eye);
    }
    document.querySelectorAll('input[type=text], input[type=password]').forEach(function(inp) {
      var parent = inp.parentNode;
      if (!parent.classList.contains('field-group') && !parent.classList.contains('token-wrap')) {
        var grp = document.createElement('div');
        grp.className = 'field-group';
        parent.insertBefore(grp, inp);
        grp.appendChild(inp);
      }
    });
  });
  function copyFormat() {
    navigator.clipboard && navigator.clipboard.writeText('Token ').then(function() {
      var btn = event.target;
      btn.textContent = '✅ Tersalin!';
      setTimeout(function() { btn.textContent = '📋 Salin Format'; }, 2000);
    });
  }
</script>
)rawliteral";

// ============================================================
//  CALLBACK
// ============================================================
void saveConfigCallback() {
  Serial.println(F("[Config] Konfigurasi baru terdeteksi, akan disimpan..."));
  shouldSaveConfig = true;
}

// ============================================================
//  FUNGSI: Load konfigurasi dari LittleFS
// ============================================================
void loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println(F("[Config] File config tidak ditemukan, gunakan default."));
    return;
  }
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) { Serial.println(F("[Config] Gagal membuka file config!")); return; }
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { Serial.printf("[Config] JSON parse error: %s\n", err.c_str()); return; }
  if (doc.containsKey("apiToken")) {
    strlcpy(apiToken, doc["apiToken"], sizeof(apiToken));
    Serial.println(F("[Config] Token berhasil dimuat dari flash."));
    Serial.printf("[Config] Token preview: %.20s...\n", apiToken);
  }
}

// ============================================================
//  FUNGSI: Simpan konfigurasi ke LittleFS
// ============================================================
void saveConfig() {
  StaticJsonDocument<512> doc;
  doc["apiToken"] = apiToken;
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) { Serial.println(F("[Config] Gagal membuka file untuk ditulis!")); return; }
  serializeJson(doc, f);
  f.close();
  Serial.println(F("[Config] Konfigurasi tersimpan ke flash."));
}

// ============================================================
//  FUNGSI: Setup Local HTTP Server (Device Discovery)
//  Endpoint GET /info → return JSON identitas device
//  Browser atau Django proxy bisa fetch endpoint ini
// ============================================================
void setupLocalServer() {
  // ── GET /info ── identitas device untuk discovery
  localServer.on("/info", HTTP_GET, []() {
    localServer.sendHeader("Access-Control-Allow-Origin",  "*");
    localServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    localServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    StaticJsonDocument<300> doc;
    doc["device_id"]   = WiFi.macAddress();
    doc["device_name"] = AP_SSID;
    doc["firmware"]    = "4.0";
    doc["sensor"]      = "TCS34725";
    doc["ip"]          = WiFi.localIP().toString();
    doc["ssid"]        = WiFi.SSID();
    doc["rssi"]        = WiFi.RSSI();
    doc["uptime_ms"]   = millis();

    String resp;
    serializeJson(doc, resp);
    localServer.send(200, "application/json", resp);
    Serial.println(F("[LocalServer] /info requested → responded"));
  });

  // ── OPTIONS /info ── CORS preflight
  localServer.on("/info", HTTP_OPTIONS, []() {
    localServer.sendHeader("Access-Control-Allow-Origin",  "*");
    localServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    localServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    localServer.send(204);
  });

  // ── GET / ── halaman status sederhana
  localServer.on("/", HTTP_GET, []() {
    String html = "<html><body style='font-family:monospace;background:#060912;color:#e6edf3;padding:20px'>";
    html += "<h2>ESP32 Color Sensor v4.0</h2>";
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    html += "<p>SSID: " + WiFi.SSID() + "</p>";
    html += "<p>RSSI: " + String(WiFi.RSSI()) + " dBm</p>";
    html += "<p>MAC: " + WiFi.macAddress() + "</p>";
    html += "<p>Uptime: " + String(millis()/1000) + "s</p>";
    html += "<p><a href='/info' style='color:#38BDF8'>GET /info (JSON)</a></p>";
    html += "</body></html>";
    localServer.send(200, "text/html", html);
  });

  localServer.begin();
  Serial.println(F("[LocalServer] ✅ HTTP server aktif di port 80"));
  Serial.printf("[LocalServer] Discovery: http://%s/info\n",
                WiFi.localIP().toString().c_str());
  Serial.printf("[LocalServer] mDNS:      http://%s.local/info\n", MDNS_NAME);
}

// ============================================================
//  FUNGSI: Kirim heartbeat ke Django (auto-register device)
// ============================================================
void sendHeartbeat() {
  String authHeader = String(apiToken);
  authHeader.trim();
  if (!authHeader.startsWith("Token ") && !authHeader.startsWith("Bearer ")) {
    authHeader = "Token " + authHeader;
  }

  StaticJsonDocument<256> doc;
  doc["device_id"]   = WiFi.macAddress();
  doc["device_name"] = AP_SSID;
  doc["ip_local"]    = WiFi.localIP().toString();
  doc["ssid"]        = WiFi.SSID();
  doc["rssi"]        = WiFi.RSSI();
  doc["firmware"]    = "4.0";

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(HEARTBEAT_URL);
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("Authorization", authHeader);
  http.setTimeout(5000);

  int code = http.POST(body);
  if (code == 200 || code == 201) {
    Serial.println(F("[Heartbeat] ✅ Registered ke Django"));
  } else {
    Serial.printf("[Heartbeat] ⚠ Status: %d\n", code);
  }
  http.end();
}

// ============================================================
//  FUNGSI: Setup WiFiManager
// ============================================================
void startWiFiManager(bool forcePortal = false) {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalTimeout(180);
  wm.setCustomHeadElement(CUSTOM_HEAD);
  wm.setCustomMenuHTML(CUSTOM_BODY);

  WiFiManagerParameter param_token(
    "token",
    "API Token (format: Token xxxxx)",
    apiToken,
    148
  );
  wm.addParameter(&param_token);

  bool connected = false;
  if (forcePortal) {
    Serial.printf("[WiFi] Membuka portal konfigurasi: SSID=%s\n", AP_SSID);
    connected = wm.startConfigPortal(AP_SSID, AP_PASSWORD);
  } else {
    Serial.printf("[WiFi] Mencoba autoConnect... Hotspot: %s\n", AP_SSID);
    connected = wm.autoConnect(AP_SSID, AP_PASSWORD);
  }

  if (!connected) {
    Serial.println(F("[WiFi] Gagal konek & portal timeout. Restart..."));
    delay(3000);
    ESP.restart();
  }

  strlcpy(apiToken, param_token.getValue(), sizeof(apiToken));
  if (shouldSaveConfig) { saveConfig(); shouldSaveConfig = false; }

  reconnectAttempts = 0;

  Serial.println(F("[WiFi] ✅ Terhubung ke WiFi!"));
  Serial.printf("[WiFi] IP      : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WiFi] Gateway : %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("[Token] Aktif  : %.30s...\n\n", apiToken);

  if (MDNS.begin(MDNS_NAME)) {
    // Daftarkan service mDNS agar bisa di-discover
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] ✅ http://%s.local aktif\n", MDNS_NAME);
  } else {
    Serial.println(F("[mDNS] Gagal start mDNS."));
  }
}

// ============================================================
//  FUNGSI: Handle reconnect dengan exponential backoff
// ============================================================
void handleReconnect() {
  reconnectAttempts++;
  unsigned long delayMs = RECONNECT_BASE_DELAY * (1UL << min(reconnectAttempts - 1, 4));
  Serial.printf("[WiFi] Koneksi putus. Percobaan %d/%d — tunggu %lu ms...\n",
                reconnectAttempts, MAX_RECONNECT_ATTEMPTS, delayMs);

  WiFi.reconnect();
  delay(delayMs);

  if (WiFi.status() == WL_CONNECTED) {
    reconnectAttempts = 0;
    Serial.println(F("[WiFi] ✅ Reconnect berhasil!"));
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    MDNS.end();
    if (MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[mDNS] ✅ mDNS aktif: http://%s.local\n", MDNS_NAME);
    }
    // Re-register ke Django setelah reconnect
    sendHeartbeat();
    return;
  }

  if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
    Serial.println(F("[WiFi] ⚠  Gagal reconnect. Membuka portal WiFi Manager..."));
    reconnectAttempts = 0;
    startWiFiManager(true);
  }
}

// ============================================================
//  FUNGSI: Kirim data sensor ke Django REST API
// ============================================================
void sendSensorData() {
  uint16_t r, g, b, c;
  uint16_t colorTemp, lux;

  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux       = tcs.calculateLux(r, g, b);

  Serial.printf("[Sensor] C:%u  R:%u  G:%u  B:%u  |  Lux:%u  Temp:%uK\n",
                c, r, g, b, lux, colorTemp);

  StaticJsonDocument<256> doc;
  doc["raw_light"] = c;
  doc["red"]       = r;
  doc["green"]     = g;
  doc["blue"]      = b;
  doc["temp"]      = colorTemp;
  doc["lux"]       = lux;

  String body;
  serializeJson(doc, body);

  String authHeader = String(apiToken);
  authHeader.trim();
  if (!authHeader.startsWith("Token ") && !authHeader.startsWith("Bearer ")) {
    authHeader = "Token " + authHeader;
  }

  Serial.printf("[Token] Header : %.40s...\n", authHeader.c_str());

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("Authorization", authHeader);
  http.setTimeout(8000);

  Serial.printf("[HTTP] POST → %s\n", SERVER_URL);
  int code = http.POST(body);

  if (code > 0) {
    String resp = http.getString();
    Serial.printf("[HTTP] Status: %d | Response: %s\n", code, resp.c_str());
    if      (code == 201) Serial.println(F("[HTTP] ✅ Data berhasil dikirim!"));
    else if (code == 400) Serial.println(F("[HTTP] ⚠  Bad Request!"));
    else if (code == 401 || code == 403) Serial.println(F("[HTTP] 🔒 Unauthorized!"));
    else if (code >= 500) Serial.println(F("[HTTP] 🔴 Server error!"));
  } else {
    Serial.printf("[HTTP] ❌ Error koneksi: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  delay(500);

  Serial.println(F("\n\n╔══════════════════════════════════════╗"));
  Serial.println(F("║   ESP32 IoT Color Sensor  v4.0       ║"));
  Serial.println(F("╠══════════════════════════════════════╣"));
  Serial.println(F("║  + Local HTTP Server (discovery)     ║"));
  Serial.println(F("║  + Heartbeat to Django               ║"));
  Serial.println(F("╚══════════════════════════════════════╝\n"));

  // 1. Mount filesystem
  if (!LittleFS.begin(true)) {
    Serial.println(F("[FS] ❌ LittleFS mount GAGAL!"));
    return;
  }
  Serial.println(F("[FS] ✅ LittleFS OK."));

  // 2. Load token dari flash
  loadConfig();

  // 3. Cek tombol reset (tahan BOOT saat power-on)
  if (digitalRead(TRIGGER_PIN) == LOW) {
    Serial.println(F("[Reset] 🔄 Tombol BOOT ditekan — hapus WiFi & Token..."));
    WiFiManager wm;
    wm.resetSettings();
    LittleFS.remove(CONFIG_FILE);
    Serial.println(F("[Reset] ✅ Selesai. Restart dalam 2 detik..."));
    delay(2000);
    ESP.restart();
  }

  // 4. Setup WiFiManager
  startWiFiManager(false);

  // 5. Setup local HTTP server untuk discovery
  setupLocalServer();

  // 6. Kirim heartbeat pertama segera setelah connect
  sendHeartbeat();
  lastHeartbeat = millis();

  // 7. Inisialisasi sensor TCS34725
  if (!tcs.begin()) {
    Serial.println(F("[Sensor] ❌ TCS34725 tidak terdeteksi! Cek kabel I2C."));
    while (1) delay(1000);
  }
  Serial.println(F("[Sensor] ✅ TCS34725 siap."));

  Serial.println(F("\n──────────────────────────────────────"));
  Serial.println(F("  Sistem siap. Loop dimulai..."));
  Serial.printf ("  Discovery URL: http://%s/info\n", WiFi.localIP().toString().c_str());
  Serial.println(F("──────────────────────────────────────\n"));
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // ── Cek tombol BOOT ──
  if (digitalRead(TRIGGER_PIN) == LOW) {
    delay(50);
    if (digitalRead(TRIGGER_PIN) == LOW) {
      Serial.println(F("[Reset] Tombol BOOT ditekan — buka portal WiFi..."));
      delay(3000);
      if (digitalRead(TRIGGER_PIN) == LOW) {
        Serial.println(F("[Reset] Reset penuh WiFi + Token..."));
        WiFiManager wm;
        wm.resetSettings();
        LittleFS.remove(CONFIG_FILE);
        delay(1000);
        ESP.restart();
      }
      startWiFiManager(true);
      return;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();

    // ── Handle HTTP request dari browser/proxy (PENTING: harus non-blocking) ──
    localServer.handleClient();

    // ── Heartbeat setiap 15 detik ──
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
      sendHeartbeat();
      lastHeartbeat = now;
    }

    // ── Kirim sensor data setiap 10 detik ──
    if (now - lastSensor >= SEND_INTERVAL) {
      sendSensorData();
      lastSensor = now;
    }

  } else {
    handleReconnect();
  }
}