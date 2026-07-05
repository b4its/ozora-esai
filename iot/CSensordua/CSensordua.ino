/**
 * ============================================================
 *  ESP32 + TCS34725 Color Sensor — IoT Data Sender
 *  Version : 5.0
 *  Features:
 *    - WiFiManager portal (startConfigPortal) setiap boot
 *    - Token API di memori saja (tidak disimpan di flash)
 *    - Local HTTP Server port 80 untuk device discovery
 *    - Heartbeat + kirim data sensor ke Django REST API
 *    - LED iluminasi TCS via GPIO 4
 *    - I2C eksplisit: SDA=GPIO21, SCL=GPIO22
 *    - Jika WiFi putus -> reset semua setting -> restart -> portal
 *    - mDNS: akses portal via http://esp32sensor.local
 * ============================================================
 *
 *  Library:
 *    1. WiFiManager by tzapu
 *    2. ArduinoJson by bblanchon
 *    3. Adafruit TCS34725
 *    4. Adafruit BusIO (dependensi)
 *    5. ESPmDNS (built-in)
 *    6. WebServer (built-in)
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "Adafruit_TCS34725.h"

// ============================================================
//  KONFIGURASI
// ============================================================

const char* AP_SSID      = "ESP32_Sensor_Color";
const char* AP_PASSWORD  = "Admin1234";
const char* MDNS_NAME    = "esp32sensor";
const char* SERVER_URL   = "https://ozora.b4its.cloud/api/receive-data/";
const char* HEARTBEAT_URL = "https://ozora.b4its.cloud/api/device/heartbeat/";

#define SDA_PIN          21
#define SCL_PIN          22
#define TCS_LED          4

#define SEND_INTERVAL      10000
#define HEARTBEAT_INTERVAL 15000

// ============================================================
//  VARIABEL GLOBAL
// ============================================================

char apiToken[150]        = "";
unsigned long lastHeartbeat = 0;
unsigned long lastSensor    = 0;

WebServer localServer(80);

Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);

// ============================================================
//  CUSTOM HTML PORTAL WIFIMANAGER
// ============================================================

const char CUSTOM_HEAD[] PROGMEM = R"rawliteral(
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap');
  :root {
    --bg:#060912;--surface:#0d1117;--card:#161b27;--border:#21293d;--border2:#2d3a52;
    --text:#e6edf3;--muted:#8b949e;--accent:#4f8ef7;--accent2:#6366f1;--success:#3fb950;--glow:rgba(79,142,247,0.18);
  }
  *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Inter',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px 16px 40px;background-image:radial-gradient(ellipse 80% 50% at 50% -20%,rgba(79,142,247,.08) 0%,transparent 70%)}
  .top-nav{width:100%;max-width:460px;display:flex;align-items:center;justify-content:space-between;margin-bottom:28px}
  .brand{display:flex;align-items:center;gap:10px}
  .brand-icon{width:36px;height:36px;border-radius:10px;background:linear-gradient(135deg,var(--accent),var(--accent2));display:flex;align-items:center;justify-content:center;font-size:18px;box-shadow:0 4px 14px var(--glow)}
  .brand-name{font-size:15px;font-weight:700}
  .brand-sub{font-size:11px;color:var(--muted)}
  .status-pill{display:flex;align-items:center;gap:7px;background:rgba(63,185,80,.1);border:1px solid rgba(63,185,80,.25);border-radius:99px;padding:5px 12px;font-size:12px;font-weight:500;color:var(--success)}
  .pulse-dot{width:7px;height:7px;border-radius:50%;background:var(--success);animation:pulse 1.8s ease-in-out infinite}
  @keyframes pulse{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.4;transform:scale(1.3)}}
  .card{width:100%;max-width:460px;background:var(--card);border:1px solid var(--border);border-radius:18px;padding:28px 26px;box-shadow:0 0 0 1px rgba(255,255,255,.03),0 24px 64px rgba(0,0,0,.55);position:relative;overflow:hidden}
  .card::before{content:'';position:absolute;top:0;left:0;right:0;height:1px;background:linear-gradient(90deg,transparent,rgba(79,142,247,.4),transparent)}
  .section-title{font-size:18px;font-weight:700;letter-spacing:-0.4px;margin-bottom:3px}
  .section-sub{font-size:13px;color:var(--muted);margin-bottom:22px;line-height:1.5}
  .chips{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:22px}
  .chip{display:inline-flex;align-items:center;gap:5px;background:rgba(255,255,255,.04);border:1px solid var(--border);border-radius:99px;padding:4px 11px;font-size:11px;color:var(--muted);font-weight:500}
  .chip.blue{background:rgba(79,142,247,.08);border-color:rgba(79,142,247,.25);color:#93b4f5}
  .field-group{margin-bottom:18px}
  label{display:flex;align-items:center;gap:6px;font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.7px;color:var(--muted);margin-bottom:7px}
  input[type=text],input[type=password]{width:100%;background:var(--surface);border:1px solid var(--border2);border-radius:11px;padding:11px 14px;font-size:14px;color:var(--text);outline:none;transition:border-color .2s,box-shadow .2s;font-family:inherit}
  input[type=text]:focus,input[type=password]:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(79,142,247,.15)}
  input::placeholder{color:#3d4f69}
  .token-wrap{position:relative}
  .token-wrap input{padding-right:42px;font-family:monospace;font-size:13px}
  .token-eye{position:absolute;right:13px;top:50%;transform:translateY(-50%);cursor:pointer;color:var(--muted);font-size:16px;background:none;border:none;padding:2px;transition:color .15s;width:auto!important;margin:0!important}
  .token-eye:hover{color:var(--accent)}
  .hint-box{background:rgba(79,142,247,.06);border:1px solid rgba(79,142,247,.2);border-left:3px solid var(--accent);border-radius:10px;padding:12px 14px;font-size:12px;color:#7aa3e8;margin-bottom:20px;line-height:1.65}
  .hint-box strong{color:#a5c0f0;display:block;margin-bottom:4px;font-weight:600}
  .hint-box code{background:rgba(79,142,247,.15);border-radius:5px;padding:2px 6px;font-family:monospace;font-size:11.5px;color:#bdd4f8}
  .copy-btn{display:inline-flex;align-items:center;gap:4px;background:rgba(79,142,247,.12);border:1px solid rgba(79,142,247,.25);color:var(--accent);border-radius:6px;padding:3px 9px;font-size:11px;font-weight:600;cursor:pointer;margin-top:7px;width:auto!important;transition:background .15s}
  .copy-btn:hover{background:rgba(79,142,247,.22)}
  input[type=submit]{width:100%;background:linear-gradient(135deg,var(--accent) 0%,var(--accent2) 100%);border:none;border-radius:11px;padding:13px;font-size:15px;font-weight:600;color:#fff;cursor:pointer;transition:opacity .2s,transform .1s;box-shadow:0 4px 18px rgba(79,142,247,.3);margin-top:6px}
  input[type=submit]:hover{opacity:.88}
  input[type=submit]:active{transform:scale(.98)}
  button{width:100%;background:var(--surface);border:1px solid var(--border2);border-radius:11px;padding:11px;font-size:14px;font-weight:500;color:var(--muted);cursor:pointer;transition:border-color .2s,color .2s;margin-top:4px}
  button:hover{border-color:var(--accent);color:var(--text)}
  .access-info{background:rgba(63,185,80,.06);border:1px solid rgba(63,185,80,.2);border-radius:11px;padding:12px 14px;margin-top:18px;font-size:12px;color:#6dba83;line-height:1.6}
  .access-info strong{color:#8dd4a0;display:block;margin-bottom:4px}
  .access-info .url-badge{display:inline-block;background:rgba(63,185,80,.1);border:1px solid rgba(63,185,80,.2);border-radius:6px;padding:2px 8px;font-family:monospace;font-size:12px;color:#a8e6b8}
  .footer{margin-top:22px;font-size:11px;color:#2d3a52;text-align:center;line-height:1.8}
  @media(max-width:480px){.card{padding:22px 18px;border-radius:14px}}
</style>
)rawliteral";

const char CUSTOM_BODY[] PROGMEM = R"rawliteral(
<div class="top-nav">
  <div class="brand">
    <div class="brand-icon">&#x1F3A8;</div>
    <div>
      <div class="brand-name">Color Sensor</div>
      <div class="brand-sub">ESP32 IoT Device v5.0</div>
    </div>
  </div>
  <div class="status-pill"><span class="pulse-dot"></span>AP Mode</div>
</div>
<div class="card">
  <div class="section-title">Konfigurasi Perangkat</div>
  <p class="section-sub">Hubungkan ke WiFi dan masukkan API Token.</p>
  <div class="chips">
    <span class="chip">&#x1F4E1; TCS34725 RGB</span>
    <span class="chip blue">&#x26A1; ESP32</span>
    <span class="chip">&#x1F504; v5.0</span>
  </div>
  <div class="hint-box">
    <strong>&#x1F511; API Token</strong>
    Paste token dari Dashboard. Prefix <code>Token </code> ditambah otomatis.<br>
    <button class="copy-btn" onclick="copyFormat()">&#x1F4CB; Salin Format</button>
  </div>
  {v}
  <div class="access-info">
    <strong>&#x1F310; Mode Sesi Sekali Pakai</strong>
    Jika WiFi terputus, semua setting direset dan perangkat kembali ke mode AP.
  </div>
</div>
<div class="footer">
  ESP32 Color Sensor v5.0<br>
  <a href="/wifi">Scan WiFi</a> &nbsp;&middot;&nbsp; <a href="/i">Info</a>
</div>
<script>
  function toggleToken(){var i=document.getElementById('token'),b=document.getElementById('eyeBtn');if(!i)return;i.type=i.type==='password'?'text':'password';b.textContent=i.type==='password'?'&#x1F441;':'&#x1F648;'}
  window.addEventListener('DOMContentLoaded',function(){
    var i=document.getElementById('token')||document.querySelector('input[name=token]');
    if(i){
      i.type='password';i.placeholder='Token xxxxxxxxxxxxxxxxxxxxxxxx';
      var w=document.createElement('div');w.className='token-wrap';
      i.parentNode.insertBefore(w,i);w.appendChild(i);
      var e=document.createElement('button');e.id='eyeBtn';e.className='token-eye';e.type='button';e.textContent='&#x1F441;';e.onclick=toggleToken;
      w.appendChild(e);
    }
    document.querySelectorAll('input[type=text],input[type=password]').forEach(function(x){
      var p=x.parentNode;if(!p.classList.contains('field-group')&&!p.classList.contains('token-wrap')){var g=document.createElement('div');g.className='field-group';p.insertBefore(g,x);g.appendChild(x)}
    });
  });
  function copyFormat(){navigator.clipboard&&navigator.clipboard.writeText('Token ').then(function(){var b=event.target;b.textContent='&#x2705; Tersalin!';setTimeout(function(){b.textContent='&#x1F4CB; Salin Format'},2000)})}
</script>
)rawliteral";

// ============================================================
//  FUNGSI: Kirim HTTP POST
// ============================================================
int httpPost(const char* url, const String& body) {
  String auth = String(apiToken);
  auth.trim();
  if (!auth.startsWith("Token ") && !auth.startsWith("Bearer ")) {
    auth = "Token " + auth;
  }

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("Authorization", auth);
  http.setTimeout(8000);

  int code = http.POST(body);
  if (code > 0) {
    Serial.printf("[HTTP] %s -> %d\n", url, code);
  } else {
    Serial.printf("[HTTP] %s - %s\n", url, http.errorToString(code).c_str());
  }
  http.end();
  return code;
}

// ============================================================
//  FUNGSI: Local HTTP Server (Device Discovery)
// ============================================================
void setupLocalServer() {
  localServer.on("/info", HTTP_GET, []() {
    localServer.sendHeader("Access-Control-Allow-Origin",  "*");
    localServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    localServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    StaticJsonDocument<300> doc;
    doc["device_id"]   = WiFi.macAddress();
    doc["device_name"] = AP_SSID;
    doc["firmware"]    = "5.0";
    doc["sensor"]      = "TCS34725";
    doc["ip"]          = WiFi.localIP().toString();
    doc["ssid"]        = WiFi.SSID();
    doc["rssi"]        = WiFi.RSSI();
    doc["uptime_ms"]   = millis();

    String resp;
    serializeJson(doc, resp);
    localServer.send(200, "application/json", resp);
  });

  localServer.on("/info", HTTP_OPTIONS, []() {
    localServer.sendHeader("Access-Control-Allow-Origin",  "*");
    localServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    localServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    localServer.send(204);
  });

  localServer.on("/", HTTP_GET, []() {
    String html = "<html><body style='font-family:monospace;background:#060912;color:#e6edf3;padding:20px'>";
    html += "<h2>ESP32 Color Sensor v5.0</h2>";
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
  Serial.printf("[LocalServer] http://%s/info\n", WiFi.localIP().toString().c_str());
}

// ============================================================
//  FUNGSI: Heartbeat (daftarkan device)
// ============================================================
void sendHeartbeat() {
  StaticJsonDocument<256> doc;
  doc["device_id"]   = WiFi.macAddress();
  doc["device_name"] = AP_SSID;
  doc["ip_local"]    = WiFi.localIP().toString();
  doc["ssid"]        = WiFi.SSID();
  doc["rssi"]        = WiFi.RSSI();
  doc["firmware"]    = "5.0";

  String body;
  serializeJson(doc, body);
  int code = httpPost(HEARTBEAT_URL, body);
  if (code == 200 || code == 201) {
    Serial.println(F("[Heartbeat] Registered"));
  }
}

// ============================================================
//  FUNGSI: Kirim data sensor
// ============================================================
void sendSensorData() {
  uint16_t r, g, b, c;
  uint16_t colorTemp, lux;

  digitalWrite(TCS_LED, HIGH);
  delay(50);

  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux       = tcs.calculateLux(r, g, b);

  digitalWrite(TCS_LED, LOW);

  Serial.printf("[Sensor] C:%u  R:%u  G:%u  B:%u  |  Lux:%u  Temp:%uK\n",
                c, r, g, b, lux, colorTemp);

  StaticJsonDocument<256> doc;
  doc["device_id"] = WiFi.macAddress();
  doc["raw_light"] = c;
  doc["red"]       = r;
  doc["green"]     = g;
  doc["blue"]      = b;
  doc["temp"]      = colorTemp;
  doc["lux"]       = lux;

  String body;
  serializeJson(doc, body);
  httpPost(SERVER_URL, body);
}

// ============================================================
//  FUNGSI: Factory Reset
// ============================================================
void factoryReset() {
  Serial.println(F("\nWiFi PUTUS - Reset semua setting..."));
  WiFiManager wm;
  wm.resetSettings();
  apiToken[0] = '\0';
  Serial.println(F("[Reset] WiFi & Token dihapus. Restart..."));
  delay(2000);
  ESP.restart();
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n========================================"));
  Serial.println(F("  ESP32 IoT Color Sensor  v5.0"));
  Serial.println(F("  I2C Pins: SDA=GPIO21, SCL=GPIO22"));
  Serial.println(F("========================================\n"));

  Serial.printf("[Device] MAC: %s\n", WiFi.macAddress().c_str());

  // LED iluminasi
  pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_LED, LOW);
  Serial.printf("[LED] GPIO%d initialized (OFF)\n", TCS_LED);

  // Portal WiFiManager
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setCustomHeadElement(CUSTOM_HEAD);
  wm.setCustomMenuHTML(CUSTOM_BODY);

  WiFiManagerParameter param_token("token", "API Token", apiToken, 148);
  wm.addParameter(&param_token);

  Serial.printf("[WiFi] Buka portal: SSID=%s\n", AP_SSID);
  if (!wm.startConfigPortal(AP_SSID, AP_PASSWORD)) {
    Serial.println(F("[WiFi] Portal timeout. Restart..."));
    delay(3000);
    ESP.restart();
  }

  strlcpy(apiToken, param_token.getValue(), sizeof(apiToken));
  Serial.println(F("[WiFi] Terhubung!"));
  Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[Token] %.30s...\n\n", apiToken);

  // mDNS
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local\n", MDNS_NAME);
  }

  // Local HTTP server
  setupLocalServer();

  // I2C eksplisit
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.printf("[I2C] Wire.begin(SDA=%d, SCL=%d)\n", SDA_PIN, SCL_PIN);

  // Heartbeat pertama
  sendHeartbeat();
  lastHeartbeat = millis();

  // Sensor
  if (!tcs.begin()) {
    Serial.println(F("[Sensor] TCS34725 tidak terdeteksi! Cek I2C."));
    while (1) delay(1000);
  }
  Serial.println(F("[Sensor] TCS34725 siap.\n"));
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();

    localServer.handleClient();

    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
      sendHeartbeat();
      lastHeartbeat = now;
    }

    if (now - lastSensor >= SEND_INTERVAL) {
      sendSensorData();
      lastSensor = now;
    }

  } else {
    factoryReset();
  }
}