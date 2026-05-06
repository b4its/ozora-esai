/**
 * ============================================================
 *  ESP32 + TCS34725 + AC Dimmer + Relays — IoT Web Controlled
 *  Version : 4.0.1 (Ozora System - Always On Portal)
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <LiquidCrystal_I2C.h>
#include "Adafruit_TCS34725.h"

// ============================================================
//  KONFIGURASI JARINGAN & SERVER
// ============================================================
const char* AP_SSID     = "ESP32_Ozora_System";
const char* MDNS_NAME   = "esp32ozora";

const char* SERVER_URL_DATA      = "https://ozora.b4its.cloud/api/receive-data/";
const char* SERVER_URL_HEARTBEAT = "https://ozora.b4its.cloud/api/device/heartbeat/";

#define TRIGGER_PIN 0
#define CONFIG_FILE "/config.json" 

#define HEARTBEAT_INTERVAL 15000 
#define SENSOR_INTERVAL    10000 

#define MAX_RECONNECT_ATTEMPTS 3
#define RECONNECT_BASE_DELAY   3000

// ============================================================
//  DEFINISI PIN HARDWARE
// ============================================================
#define TCS_LED_PIN 4
#define LED_HIJAU_PIN 19
#define RELAY_1_PIN 25  
#define RELAY_2_PIN 26  
#define RELAY_3_PIN 27  
#define DIMMER_ZC_PIN 13
#define DIMMER_DIM_PIN 14

// ============================================================
//  VARIABEL GLOBAL & INSTANCE WIFIMANAGER BACKGROUND
// ============================================================
char apiToken[150] = "";
int  reconnectAttempts  = 0;

unsigned long lastHeartbeatTime = 0;
unsigned long lastSensorTime = 0;

bool isSystemOn = false;

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

// [KUNCI 1] Pindahkan Instance WiFiManager ke Global Scope
WiFiManager wm;
WiFiManagerParameter param_token("token", "API Token (format: Token xxxxx)", apiToken, 148);

// ============================================================
//  CUSTOM DRIVER AC DIMMER ESP32 V3
// ============================================================
volatile int currentDimmerPower = 0; 
hw_timer_t *dimmerTimer = NULL;

void IRAM_ATTR onDimmerTimer() {
  digitalWrite(DIMMER_DIM_PIN, HIGH);
  for(int i=0; i<500; i++) { asm volatile ("nop"); }
  digitalWrite(DIMMER_DIM_PIN, LOW);
}

void IRAM_ATTR zcDetectISR() {
  if (currentDimmerPower <= 0) {
    digitalWrite(DIMMER_DIM_PIN, LOW);
    return;
  }
  if (currentDimmerPower >= 100) {
    digitalWrite(DIMMER_DIM_PIN, HIGH);
    return;
  }
  uint32_t delayTime = 100 * (100 - currentDimmerPower);
  if (delayTime < 200) delayTime = 200; 
  if (delayTime > 9800) delayTime = 9800; 
  timerWrite(dimmerTimer, 0);
  timerAlarm(dimmerTimer, delayTime, false, 0); 
}

// ============================================================
//  PORTAL WIFIMANAGER (HTML)
// ============================================================
const char CUSTOM_HEAD[] PROGMEM = R"rawliteral(
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap');
  :root { --bg: #060912; --surface: #0d1117; --card: #161b27; --border: #21293d; --border2: #2d3a52; --text: #e6edf3; --muted: #8b949e; --accent: #4f8ef7; --accent2: #6366f1; --success: #3fb950; --warning: #d29922; --glow: rgba(79,142,247,0.18); }
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: 'Inter', system-ui, sans-serif; background: var(--bg); color: var(--text); min-height: 100vh; display: flex; flex-direction: column; align-items: center; padding: 20px 16px 40px; background-image: radial-gradient(ellipse 80% 50% at 50% -20%, rgba(79,142,247,.08) 0%, transparent 70%); }
  .top-nav { width: 100%; max-width: 460px; display: flex; align-items: center; justify-content: space-between; margin-bottom: 28px; }
  .brand { display: flex; align-items: center; gap: 10px; }
  .brand-icon { width: 36px; height: 36px; border-radius: 10px; background: linear-gradient(135deg, var(--accent), var(--accent2)); display: flex; align-items: center; justify-content: center; font-size: 18px; box-shadow: 0 4px 14px var(--glow); }
  .brand-name { font-size: 15px; font-weight: 700; color: var(--text); }
  .brand-sub  { font-size: 11px; color: var(--muted); }
  .status-pill { display: flex; align-items: center; gap: 7px; background: rgba(63,185,80,.1); border: 1px solid rgba(63,185,80,.25); border-radius: 99px; padding: 5px 12px; font-size: 12px; font-weight: 500; color: var(--success); }
  .pulse-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--success); animation: pulse 1.8s ease-in-out infinite; }
  @keyframes pulse { 0%,100% { opacity:1; transform:scale(1); } 50% { opacity:.4; transform:scale(1.3); } }
  .card { width: 100%; max-width: 460px; background: var(--card); border: 1px solid var(--border); border-radius: 18px; padding: 28px 26px; box-shadow: 0 0 0 1px rgba(255,255,255,.03), 0 24px 64px rgba(0,0,0,.55); position: relative; overflow: hidden; }
  .card::before { content: ''; position: absolute; top: 0; left: 0; right: 0; height: 1px; background: linear-gradient(90deg, transparent, rgba(79,142,247,.4), transparent); }
  .section-title { font-size: 18px; font-weight: 700; letter-spacing: -0.4px; color: var(--text); margin-bottom: 3px; }
  .section-sub { font-size: 13px; color: var(--muted); margin-bottom: 22px; line-height: 1.5; }
  .chips { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 22px; }
  .chip { display: inline-flex; align-items: center; gap: 5px; background: rgba(255,255,255,.04); border: 1px solid var(--border); border-radius: 99px; padding: 4px 11px; font-size: 11px; color: var(--muted); font-weight: 500; }
  .chip.blue { background: rgba(79,142,247,.08); border-color: rgba(79,142,247,.25); color: #93b4f5; }
  .field-group { margin-bottom: 18px; }
  label { display: flex; align-items: center; gap: 6px; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: .7px; color: var(--muted); margin-bottom: 7px; }
  input[type=text], input[type=password] { width: 100%; background: var(--surface); border: 1px solid var(--border2); border-radius: 11px; padding: 11px 14px; font-size: 14px; color: var(--text); outline: none; transition: border-color .2s, box-shadow .2s; font-family: inherit; }
  input[type=text]:focus, input[type=password]:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(79,142,247,.15); }
  .token-wrap { position: relative; }
  .token-wrap input { padding-right: 42px; font-family: 'JetBrains Mono', 'Courier New', monospace; font-size: 13px; }
  .token-eye { position: absolute; right: 13px; top: 50%; transform: translateY(-50%); cursor: pointer; color: var(--muted); font-size: 16px; background: none; border: none; padding: 2px; transition: color .15s; width: auto !important; margin: 0 !important; }
  .token-eye:hover { color: var(--accent); }
  .hint-box { background: rgba(79,142,247,.06); border: 1px solid rgba(79,142,247,.2); border-left: 3px solid var(--accent); border-radius: 10px; padding: 12px 14px; font-size: 12px; color: #7aa3e8; margin-bottom: 20px; line-height: 1.65; }
  .hint-box strong { color: #a5c0f0; display: block; margin-bottom: 4px; font-weight: 600; }
  .hint-box code { background: rgba(79,142,247,.15); border-radius: 5px; padding: 2px 6px; font-family: 'JetBrains Mono', monospace; font-size: 11.5px; color: #bdd4f8; }
  .copy-btn { display: inline-flex; align-items: center; gap: 4px; background: rgba(79,142,247,.12); border: 1px solid rgba(79,142,247,.25); color: var(--accent); border-radius: 6px; padding: 3px 9px; font-size: 11px; font-weight: 600; cursor: pointer; width: auto !important; margin-top: 7px !important; transition: background .15s; }
  .copy-btn:hover { background: rgba(79,142,247,.22); }
  input[type=submit] { width: 100%; background: linear-gradient(135deg, var(--accent) 0%, var(--accent2) 100%); border: none; border-radius: 11px; padding: 13px; font-size: 15px; font-weight: 600; color: #fff; cursor: pointer; transition: opacity .2s, transform .1s, box-shadow .2s; box-shadow: 0 4px 18px rgba(79,142,247,.3); margin-top: 6px; }
  input[type=submit]:hover  { opacity: .88; box-shadow: 0 6px 24px rgba(79,142,247,.4); }
  input[type=submit]:active { transform: scale(.98); }
  button { width: 100%; background: var(--surface); border: 1px solid var(--border2); border-radius: 11px; padding: 11px; font-size: 14px; font-weight: 500; color: var(--muted); cursor: pointer; transition: border-color .2s, color .2s; margin-top: 4px; }
  button:hover { border-color: var(--accent); color: var(--text); }
  .access-info { background: rgba(63,185,80,.06); border: 1px solid rgba(63,185,80,.2); border-radius: 11px; padding: 12px 14px; margin-top: 18px; font-size: 12px; color: #6dba83; line-height: 1.6; }
  .access-info strong { color: #8dd4a0; display: block; margin-bottom: 4px; }
  .access-info .url-badge { display: inline-block; background: rgba(63,185,80,.1); border: 1px solid rgba(63,185,80,.2); border-radius: 6px; padding: 2px 8px; font-family: monospace; font-size: 12px; color: #a8e6b8; }
  .footer { margin-top: 22px; font-size: 11px; color: #2d3a52; text-align: center; line-height: 1.8; }
  .footer a { color: #3d5475; text-decoration: none; }
  .footer a:hover { color: var(--accent); }
  @media (max-width: 480px) { .card { padding: 22px 18px; border-radius: 14px; } }
</style>
)rawliteral";

const char CUSTOM_BODY[] PROGMEM = R"rawliteral(
<div class="top-nav">
  <div class="brand">
    <div class="brand-icon">⚡</div>
    <div>
      <div class="brand-name">Ozora System</div>
      <div class="brand-sub">IoT Orchestrator</div>
    </div>
  </div>
  <div class="status-pill"><span class="pulse-dot"></span>AP Mode</div>
</div>
<div class="card">
  <div class="section-title">Konfigurasi Perangkat</div>
  <p class="section-sub">Hubungkan ke jaringan WiFi dan masukkan API Token untuk mengaktifkan sistem.</p>
  <div class="chips">
    <span class="chip">📡 TCS34725 & Relays</span>
    <span class="chip blue">⚡ ESP32 Core v3</span>
    <span class="chip">🔄 v4.0</span>
  </div>
  <div class="hint-box">
    <strong>🔑 API Token</strong>
    Cukup paste token hash dari Dashboard. Prefix <code>Token </code> otomatis ditambah.<br><br>
    <button class="copy-btn" onclick="copyFormat()">📋 Salin Format</button>
  </div>
  {v}
  <div class="access-info">
    <strong>🌐 Akses Portal Kapan Saja</strong>
    Akses portal via: <span class="url-badge">http://esp32ozora.local</span> atau <span class="url-badge">192.168.4.1</span>
  </div>
</div>
<div class="footer">Ozora Industrial System &nbsp;·&nbsp; v4.0</div>
<script>
  function toggleToken() {
    var inp = document.getElementById('token');
    var btn = document.getElementById('eyeBtn');
    if (!inp) return;
    if (inp.type === 'password') { inp.type = 'text'; btn.textContent = '🙈'; } 
    else { inp.type = 'password'; btn.textContent = '👁'; }
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
      var btn = event.target; btn.textContent = '✅ Tersalin!';
      setTimeout(function() { btn.textContent = '📋 Salin Format'; }, 2000);
    });
  }
</script>
)rawliteral";

// ============================================================
//  FUNGSI STARTUP & SHUTDOWN MESIN
// ============================================================
void startupMesin() {
  if (isSystemOn) return; 

  Serial.println("\n[M-CONTROL] MEMICU SEKUENSI STARTUP MESIN...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("STARTUP MESIN:");

  int jedaAman = 800; 

  lcd.setCursor(0, 1); lcd.print("Ozonator ON...  ");
  for (int p = 0; p <= 100; p += 2) {
    currentDimmerPower = p;
    delay(20);
  }

  delay(jedaAman);
  digitalWrite(RELAY_1_PIN, HIGH);
  lcd.setCursor(0, 1); lcd.print("Pompa ON        ");

  delay(jedaAman);
  digitalWrite(RELAY_2_PIN, HIGH);
  lcd.setCursor(0, 1); lcd.print("Chiller ON      ");

  delay(jedaAman);
  digitalWrite(RELAY_3_PIN, HIGH);
  lcd.setCursor(0, 1); lcd.print("Relay 3 ON      ");

  delay(jedaAman);
  digitalWrite(LED_HIJAU_PIN, HIGH);
  digitalWrite(TCS_LED_PIN, HIGH);
  
  isSystemOn = true; 
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MESIN AKTIF     ");
  lcd.setCursor(0, 1);
  lcd.print("SENSOR BERJALAN ");
}

void shutdownMesin() {
  if (!isSystemOn) return; 

  Serial.println("\n[M-CONTROL] MEMICU SEKUENSI SHUTDOWN MESIN...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SHUTDOWN MESIN:");

  int jedaShutdown = 3000; 

  lcd.setCursor(0, 1); lcd.print("Ozonator OFF... ");
  for (int p = currentDimmerPower; p >= 0; p -= 2) {
    currentDimmerPower = p;
    delay(20);
  }

  delay(jedaShutdown);
  digitalWrite(LED_HIJAU_PIN, LOW);
  digitalWrite(TCS_LED_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Sensor OFF      ");

  delay(jedaShutdown);
  digitalWrite(RELAY_1_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Pompa OFF       ");

  delay(jedaShutdown);
  digitalWrite(RELAY_2_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Chiller OFF     ");

  delay(jedaShutdown);
  digitalWrite(RELAY_3_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Relay 3 OFF     ");

  delay(jedaShutdown);
  digitalWrite(LED_HIJAU_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("LED HIJAU OFF   ");
  
  isSystemOn = false; 
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MESIN OFFLINE   ");
  lcd.setCursor(0, 1);
  lcd.print("STANDBY WEB...  ");
}

// ============================================================
//  FUNGSI SISTEM INTI
// ============================================================
void saveConfig() {
  StaticJsonDocument<512> doc;
  doc["apiToken"] = apiToken;
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

// [KUNCI 2] Ubah callback agar langsung menyimpan value dari param_token global
void saveConfigCallback() {
  Serial.println("[WIFI] Konfigurasi Tersimpan dari Portal!");
  strlcpy(apiToken, param_token.getValue(), sizeof(apiToken));
  saveConfig();
}

void loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) return;
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return;
  StaticJsonDocument<512> doc;
  if (!deserializeJson(doc, f)) {
    if (doc.containsKey("apiToken")) {
      strlcpy(apiToken, doc["apiToken"], sizeof(apiToken));
    }
  }
  f.close();
}

String getAuthHeader() {
  String auth = String(apiToken);
  auth.trim();
  if (!auth.startsWith("Token ") && !auth.startsWith("Bearer ")) {
    auth = "Token " + auth;
  }
  return auth;
}

// ============================================================
//  FUNGSI API LOKAL & CLOUD
// ============================================================

void sendHeartbeat() {
  StaticJsonDocument<256> doc;
  doc["device_id"]   = WiFi.macAddress();
  doc["device_name"] = "Ozora_Main_Node";
  doc["ip_local"]    = WiFi.localIP().toString();
  doc["ssid"]        = WiFi.SSID();
  doc["rssi"]        = WiFi.RSSI();
  doc["firmware"]    = "v4.0-WebCtrl";

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(SERVER_URL_HEARTBEAT);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", getAuthHeader());
  http.setTimeout(8000);

  int code = http.POST(body);
  if (code == 200 || code == 201) {
    String resp = http.getString();
    StaticJsonDocument<512> respDoc;
    DeserializationError err = deserializeJson(respDoc, resp);
    
    if (!err && respDoc.containsKey("target_status")) {
      bool targetStatus = respDoc["target_status"];
      
      if (targetStatus && !isSystemOn) {
        Serial.println("[SYNC] Perintah ON diterima.");
        startupMesin();
      } else if (!targetStatus && isSystemOn) {
        Serial.println("[SYNC] Perintah OFF diterima.");
        shutdownMesin();
      }
    }
  }
  http.end();
}

void sendSensorData() {
  uint16_t r, g, b, c, colorTemp, lux;
  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux       = tcs.calculateLux(r, g, b);

  lcd.setCursor(0, 0); lcd.printf("R:%-3u G:%-3u B:%-3u", r, g, b);
  lcd.setCursor(0, 1); lcd.printf("L:%-4u K:%-4u   ", lux, colorTemp);

  StaticJsonDocument<256> doc;
  doc["raw_light"] = c; doc["red"] = r; doc["green"] = g;
  doc["blue"] = b; doc["temp"] = colorTemp; doc["lux"] = lux;

  String body; serializeJson(doc, body);

  HTTPClient http;
  http.begin(SERVER_URL_DATA);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", getAuthHeader());
  http.setTimeout(8000);
  http.POST(body);
  http.end();
}

void handleReconnect() {
  reconnectAttempts++;
  unsigned long delayMs = RECONNECT_BASE_DELAY * (1UL << min(reconnectAttempts - 1, 4));
  WiFi.reconnect();
  delay(delayMs);

  if (WiFi.status() == WL_CONNECTED) {
    reconnectAttempts = 0;
    MDNS.end();
    MDNS.begin(MDNS_NAME);
  }
}

// ============================================================
//  SETUP & MAIN LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  delay(500);

  Serial.println("\n[SYSTEM] Booting Ozora Industrial IoT...");

  Wire.begin(16, 17); Wire.setClock(100000); 

  pinMode(TCS_LED_PIN, OUTPUT); pinMode(LED_HIJAU_PIN, OUTPUT);
  pinMode(RELAY_1_PIN, OUTPUT); pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT); pinMode(DIMMER_DIM_PIN, OUTPUT);
  pinMode(DIMMER_ZC_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_1_PIN, LOW); digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(RELAY_3_PIN, LOW); digitalWrite(LED_HIJAU_PIN, LOW); 
  digitalWrite(DIMMER_DIM_PIN, LOW); digitalWrite(TCS_LED_PIN, LOW);  

  dimmerTimer = timerBegin(1000000); 
  timerAttachInterrupt(dimmerTimer, &onDimmerTimer); 
  attachInterrupt(digitalPinToInterrupt(DIMMER_ZC_PIN), zcDetectISR, RISING); 
  currentDimmerPower = 0; 

  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("BOOTING OZORA...");

  LittleFS.begin(true);
  loadConfig();

  // Reset Button Check
  if (digitalRead(TRIGGER_PIN) == LOW) {
    wm.resetSettings();
    LittleFS.remove(CONFIG_FILE);
    delay(2000);
    ESP.restart();
  }

  // ====================================================================
  //  [KUNCI 3] PENGATURAN WIFIMANAGER NON-BLOCKING AGAR PORTAL TIDAK MATI
  // ====================================================================
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setCustomHeadElement(CUSTOM_HEAD);
  wm.setCustomMenuHTML(CUSTOM_BODY);
  wm.addParameter(&param_token);

  // Jadikan non-blocking! Kode akan langsung lanjut meskipun gagal connect.
  wm.setConfigPortalBlocking(false); 

  // Auto connect tanpa password di start (berusaha konek ke jaringan sebelumnya)
  if(wm.autoConnect(AP_SSID)) {
      Serial.println("[WIFI] Sukses terhubung ke jaringan!");
  } else {
      Serial.println("[WIFI] Memulai Config Portal...");
  }

  // [KUNCI PENTING] Paksa Web Portal (DNS & Port 80) tetap menyala!
  wm.startWebPortal(); 

  // Paksa agar Access Point tetap dipancarkan ke Publik (OPEN)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID);
  
  if (MDNS.begin(MDNS_NAME)) {
    Serial.printf("[mDNS] Akses portal via http://%s.local\n", MDNS_NAME);
  }

  if (!tcs.begin()) {
    Serial.println("[ERROR] TCS34725 Tidak Ditemukan!");
    lcd.clear(); lcd.print("SENSOR ERROR!");
    while (1) delay(1000);
  }

  lcd.clear(); lcd.setCursor(0, 0); lcd.print("SYSTEM READY    ");
}

void loop() {
  // [KUNCI 4] WAJIB dipanggil agar Web Server WiFiManager menangani request di background
  wm.process();

  // Cek Tombol Reset
  if (digitalRead(TRIGGER_PIN) == LOW) {
    delay(50);
    if (digitalRead(TRIGGER_PIN) == LOW) {
      delay(3000);
      if (digitalRead(TRIGGER_PIN) == LOW) {
        wm.resetSettings();
        LittleFS.remove(CONFIG_FILE);
        delay(1000);
        ESP.restart();
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    unsigned long currentMillis = millis();

    if (currentMillis - lastHeartbeatTime >= HEARTBEAT_INTERVAL || lastHeartbeatTime == 0) {
      sendHeartbeat();
      lastHeartbeatTime = currentMillis;
    }

    if (isSystemOn && (currentMillis - lastSensorTime >= SENSOR_INTERVAL)) {
      sendSensorData();
      lastSensorTime = currentMillis;
    }

  } else {
    handleReconnect();
  }
}