/**
 * ============================================================
 *  ESP32 + TCS34725 + Relay & AC Dimmer — IoT Integrated Node
 *  Version : 3.1 (Ultimate Edition)
 *  Features:
 *    - WiFiManager (Premium UI) & API Token di LittleFS
 *    - Pengiriman Data Sensor ke Django REST API
 *    - Heartbeat Endpoint untuk sinkronisasi Kontrol (target_status)
 *    - Soft-Start / Soft-Stop AC Dimmer berdasarkan target_status
 *    - Kontrol Relay Aktuator & LCD I2C Display
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
//  KONFIGURASI API & JARINGAN
// ============================================================
const char* AP_SSID     = "ESP32_Ozora_System";
const char* AP_PASSWORD = "Admin1234"; 
const char* MDNS_NAME   = "esp32ozora";
const char* FIRMWARE_VER= "v3.1-actuator";

// URL Endpoint Django REST API
const char* DATA_URL      = "https://ozora.b4its.tech/api/receive-data/";
const char* HEARTBEAT_URL = "https://ozora.b4its.tech/api/device/heartbeat/";

// Interval Waktu (milidetik)
#define SEND_INTERVAL      10000  // Kirim data sensor tiap 10 detik
#define HEARTBEAT_INTERVAL 15000  // Sinkronisasi status (ON/OFF) tiap 15 detik

// Konfigurasi Reconnect
#define MAX_RECONNECT_ATTEMPTS  3
#define RECONNECT_BASE_DELAY    3000
#define CONFIG_FILE             "/config.json"

// ============================================================
//  DEFINISI PIN HARDWARE (Dari Kode Kalibrasi V2)
// ============================================================
#define TRIGGER_PIN    0   // Tombol BOOT untuk Reset
#define TCS_LED_PIN    4
#define LED_HIJAU_PIN  19
#define RELAY_1_PIN    25  // Pompa
#define RELAY_2_PIN    26  // Chiller
#define RELAY_3_PIN    27  // Tambahan 1
#define RELAY_4_PIN    32  // Tambahan 2
#define DIMMER_ZC_PIN  13
#define DIMMER_DIM_PIN 14

// ============================================================
//  VARIABEL GLOBAL
// ============================================================
char apiToken[150] = "";
bool shouldSaveConfig   = false;
int  reconnectAttempts  = 0;

unsigned long lastDataSent = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastDimmerUpdate = 0;

// State Kontrol dari Django
bool targetStatus = false; 

// Variabel Kontrol Dimmer (Soft-Start/Stop)
volatile int currentDimmerPower = 0; 
int targetDimmerPower = 0;           
hw_timer_t *dimmerTimer = NULL;

Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================================================
//  CUSTOM HTML WIFIMANAGER (Sama seperti V3 sebelumnya)
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
    background-image:
      radial-gradient(ellipse 80% 50% at 50% -20%, rgba(79,142,247,.08) 0%, transparent 70%);
  }

  /* ── Top Nav ── */
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
  @keyframes pulse {
    0%,100% { opacity:1; transform:scale(1); }
    50%     { opacity:.4; transform:scale(1.3); }
  }

  /* ── Card ── */
  .card {
    width: 100%; max-width: 460px;
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: 18px;
    padding: 28px 26px;
    box-shadow: 0 0 0 1px rgba(255,255,255,.03), 0 24px 64px rgba(0,0,0,.55);
    position: relative; overflow: hidden;
  }
  .card::before {
    content: '';
    position: absolute; top: 0; left: 0; right: 0; height: 1px;
    background: linear-gradient(90deg, transparent, rgba(79,142,247,.4), transparent);
  }

  /* ── Section header ── */
  .section-title {
    font-size: 18px; font-weight: 700;
    letter-spacing: -0.4px; color: var(--text);
    margin-bottom: 3px;
  }
  .section-sub {
    font-size: 13px; color: var(--muted);
    margin-bottom: 22px; line-height: 1.5;
  }

  /* ── Info chips ── */
  .chips { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 22px; }
  .chip {
    display: inline-flex; align-items: center; gap: 5px;
    background: rgba(255,255,255,.04); border: 1px solid var(--border);
    border-radius: 99px; padding: 4px 11px;
    font-size: 11px; color: var(--muted); font-weight: 500;
  }
  .chip.blue { background: rgba(79,142,247,.08); border-color: rgba(79,142,247,.25); color: #93b4f5; }

  /* ── Divider ── */
  .divider {
    display: flex; align-items: center; gap: 12px;
    margin: 22px 0;
  }
  .divider hr { flex: 1; border: none; border-top: 1px solid var(--border); }
  .divider span { font-size: 11px; color: var(--muted); white-space: nowrap; font-weight: 500; }

  /* ── Form fields ── */
  .field-group { margin-bottom: 18px; }
  label {
    display: flex; align-items: center; gap: 6px;
    font-size: 11px; font-weight: 600;
    text-transform: uppercase; letter-spacing: .7px;
    color: var(--muted); margin-bottom: 7px;
  }
  label .lbl-icon { font-size: 13px; }

  input[type=text],
  input[type=password] {
    width: 100%;
    background: var(--surface);
    border: 1px solid var(--border2);
    border-radius: 11px;
    padding: 11px 14px;
    font-size: 14px;
    color: var(--text);
    outline: none;
    transition: border-color .2s, box-shadow .2s;
    font-family: inherit;
  }
  input[type=text]:focus,
  input[type=password]:focus {
    border-color: var(--accent);
    box-shadow: 0 0 0 3px rgba(79,142,247,.15);
  }
  input[type=text]::placeholder,
  input[type=password]::placeholder { color: #3d4f69; }

  /* ── Token field special ── */
  .token-wrap { position: relative; }
  .token-wrap input { padding-right: 42px; font-family: 'JetBrains Mono', 'Courier New', monospace; font-size: 13px; }
  .token-eye {
    position: absolute; right: 13px; top: 50%; transform: translateY(-50%);
    cursor: pointer; color: var(--muted); font-size: 16px;
    background: none; border: none; padding: 2px;
    transition: color .15s;
    width: auto !important; margin: 0 !important;
  }
  .token-eye:hover { color: var(--accent); background: none; }

  /* ── Hint box ── */
  .hint-box {
    background: rgba(79,142,247,.06);
    border: 1px solid rgba(79,142,247,.2);
    border-left: 3px solid var(--accent);
    border-radius: 10px;
    padding: 12px 14px;
    font-size: 12px;
    color: #7aa3e8;
    margin-bottom: 20px;
    line-height: 1.65;
  }
  .hint-box strong { color: #a5c0f0; display: block; margin-bottom: 4px; font-weight: 600; }
  .hint-box code {
    background: rgba(79,142,247,.15);
    border-radius: 5px; padding: 2px 6px;
    font-family: 'JetBrains Mono', monospace;
    font-size: 11.5px; letter-spacing: .3px;
    color: #bdd4f8;
  }
  .copy-btn {
    display: inline-flex; align-items: center; gap: 4px;
    background: rgba(79,142,247,.12); border: 1px solid rgba(79,142,247,.25);
    color: var(--accent); border-radius: 6px;
    padding: 3px 9px; font-size: 11px; font-weight: 600;
    cursor: pointer; margin-top: 7px;
    width: auto !important; margin-top: 7px !important;
    transition: background .15s;
  }
  .copy-btn:hover { background: rgba(79,142,247,.22); }

  /* ── Submit button ── */
  input[type=submit] {
    width: 100%;
    background: linear-gradient(135deg, var(--accent) 0%, var(--accent2) 100%);
    border: none;
    border-radius: 11px;
    padding: 13px;
    font-size: 15px;
    font-weight: 600;
    color: #fff;
    cursor: pointer;
    transition: opacity .2s, transform .1s, box-shadow .2s;
    letter-spacing: .2px;
    box-shadow: 0 4px 18px rgba(79,142,247,.3);
    margin-top: 6px;
  }
  input[type=submit]:hover  { opacity: .88; box-shadow: 0 6px 24px rgba(79,142,247,.4); }
  input[type=submit]:active { transform: scale(.98); }

  /* Override default WM button styles */
  button {
    width: 100%;
    background: var(--surface);
    border: 1px solid var(--border2);
    border-radius: 11px;
    padding: 11px;
    font-size: 14px;
    font-weight: 500;
    color: var(--muted);
    cursor: pointer;
    transition: border-color .2s, color .2s;
    margin-top: 4px;
  }
  button:hover { border-color: var(--accent); color: var(--text); }

  /* ── Access info ── */
  .access-info {
    background: rgba(63,185,80,.06);
    border: 1px solid rgba(63,185,80,.2);
    border-radius: 11px;
    padding: 12px 14px;
    margin-top: 18px;
    font-size: 12px;
    color: #6dba83;
    line-height: 1.6;
  }
  .access-info strong { color: #8dd4a0; display: block; margin-bottom: 4px; }
  .access-info .url-badge {
    display: inline-block;
    background: rgba(63,185,80,.1); border: 1px solid rgba(63,185,80,.2);
    border-radius: 6px; padding: 2px 8px;
    font-family: monospace; font-size: 12px;
    color: #a8e6b8;
  }

  /* ── Footer ── */
  .footer {
    margin-top: 22px;
    font-size: 11px;
    color: #2d3a52;
    text-align: center;
    line-height: 1.8;
  }
  .footer a { color: #3d5475; text-decoration: none; }
  .footer a:hover { color: var(--accent); }

  /* ── Responsive ── */
  @media (max-width: 480px) {
    .card { padding: 22px 18px; border-radius: 14px; }
  }
</style>
)rawliteral";

const char CUSTOM_BODY[] PROGMEM = R"rawliteral(
<!-- Top Nav -->
<div class="top-nav">
  <div class="brand">
    <div class="brand-icon">🎨</div>
    <div>
      <div class="brand-name">Color Sensor</div>
      <div class="brand-sub">ESP32 IoT Device</div>
    </div>
  </div>
  <div class="status-pill">
    <span class="pulse-dot"></span>
    AP Mode
  </div>
</div>

<!-- Main Card -->
<div class="card">
  <div class="section-title">Konfigurasi Perangkat</div>
  <p class="section-sub">Hubungkan ke jaringan WiFi dan masukkan API Token untuk mulai mengirim data sensor.</p>

  <div class="chips">
    <span class="chip">📡 TCS34725 RGB Sensor</span>
    <span class="chip blue">⚡ ESP32</span>
    <span class="chip">🔄 v3.0</span>
  </div>

  <!-- Hint box -->
  <div class="hint-box">
    <strong>🔑 API Token</strong>
    Cukup paste token hash dari Dashboard. Prefix <code>Token </code> akan ditambah otomatis.<br><br>
    Contoh isi field: <code>901f60a25e09226af5dbb003859ef817...</code><br>
    Atau boleh juga dengan prefix: <code>Token 901f60a2...</code>
    <br>
    <button class="copy-btn" onclick="copyFormat()">📋 Salin Format</button>
  </div>

  <!-- WifiManager injects form here -->
  {v}

  <!-- Access info -->
  <div class="access-info">
    <strong>🌐 Akses Portal Kapan Saja</strong>
    Setelah terhubung WiFi, buka portal via:<br>
    <span class="url-badge">http://esp32sensor.local</span>
    &nbsp;atau IP: <span class="url-badge" id="localip">192.168.4.1</span>
  </div>
</div>

<div class="footer">
  ESP32 IoT Color Sensor &nbsp;·&nbsp; v3.0<br>
  Perangkat akan restart otomatis setelah konfigurasi tersimpan.<br>
  <a href="/wifi">Scan WiFi</a> &nbsp;·&nbsp; <a href="/i">Info</a> &nbsp;·&nbsp; <a href="/r">Restart</a>
</div>

<script>
  // Toggle show/hide token
  function toggleToken() {
    var inp = document.getElementById('token');
    var btn = document.getElementById('eyeBtn');
    if (!inp) return;
    if (inp.type === 'password') {
      inp.type = 'text'; btn.textContent = '🙈';
    } else {
      inp.type = 'password'; btn.textContent = '👁';
    }
  }

  // Auto-wrap token input in a styled container
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
      eye.id = 'eyeBtn';
      eye.className = 'token-eye';
      eye.type = 'button';
      eye.textContent = '👁';
      eye.onclick = toggleToken;
      wrap.appendChild(eye);
    }

    // Wrap each input in field-group
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
//  ISR: AC DIMMER ESP32
// ============================================================
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
//  FUNGSI: Sistem File & Konfigurasi
// ============================================================
void saveConfigCallback() { shouldSaveConfig = true; }

void loadConfig() {
  if (LittleFS.exists(CONFIG_FILE)) {
    File f = LittleFS.open(CONFIG_FILE, "r");
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, f) && doc.containsKey("apiToken")) {
      strlcpy(apiToken, doc["apiToken"], sizeof(apiToken));
    }
    f.close();
  }
}

void saveConfig() {
  StaticJsonDocument<512> doc;
  doc["apiToken"] = apiToken;
  File f = LittleFS.open(CONFIG_FILE, "w");
  serializeJson(doc, f);
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
//  FUNGSI: WiFiManager
// ============================================================
void startWiFiManager(bool forcePortal = false) {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalTimeout(180);
  wm.setCustomHeadElement(CUSTOM_HEAD);
  wm.setCustomMenuHTML(CUSTOM_BODY);

  WiFiManagerParameter param_token("token", "API Token", apiToken, 148);
  wm.addParameter(&param_token);

  lcd.clear(); lcd.setCursor(0,0); lcd.print("Mencari WiFi...");
  
  bool connected = forcePortal ? wm.startConfigPortal(AP_SSID, AP_PASSWORD) : wm.autoConnect(AP_SSID, AP_PASSWORD);

  if (!connected) {
    lcd.clear(); lcd.print("Gagal Konek!");
    delay(3000); ESP.restart();
  }

  strlcpy(apiToken, param_token.getValue(), sizeof(apiToken));
  if (shouldSaveConfig) { saveConfig(); shouldSaveConfig = false; }
  reconnectAttempts = 0;

  lcd.clear(); lcd.setCursor(0,0); lcd.print("WiFi Terhubung!");
  lcd.setCursor(0,1); lcd.print(WiFi.localIP().toString());
  
  if (MDNS.begin(MDNS_NAME)) {
    Serial.printf("[mDNS] ✅ http://%s.local\n", MDNS_NAME);
  }
}

// ============================================================
//  FUNGSI: Update Status Hardware (Relay)
// ============================================================
void updateHardwareState() {
  if (targetStatus) {
    // Mesin ON
    digitalWrite(RELAY_1_PIN, HIGH);
    digitalWrite(RELAY_2_PIN, HIGH);
    digitalWrite(LED_HIJAU_PIN, HIGH);
    targetDimmerPower = 100; // Target redup perlahan naik ke 100%
  } else {
    // Mesin OFF
    digitalWrite(RELAY_1_PIN, LOW);
    digitalWrite(RELAY_2_PIN, LOW);
    digitalWrite(LED_HIJAU_PIN, LOW);
    targetDimmerPower = 0;   // Target redup perlahan turun ke 0%
  }
}

// ============================================================
//  FUNGSI: Sinkronisasi Heartbeat & Kontrol (Ke Endpoint 2 Django)
// ============================================================
void sendHeartbeat() {
  StaticJsonDocument<256> doc;
  doc["device_id"]   = WiFi.macAddress();
  doc["device_name"] = AP_SSID;
  doc["ip_local"]    = WiFi.localIP().toString();
  doc["ssid"]        = WiFi.SSID();
  doc["rssi"]        = WiFi.RSSI();
  doc["firmware"]    = FIRMWARE_VER;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(HEARTBEAT_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", getAuthHeader());
  http.setTimeout(5000);

  int code = http.POST(body);
  if (code == 200 || code == 201) {
    String resp = http.getString();
    StaticJsonDocument<256> respDoc;
    deserializeJson(respDoc, resp);
    
    bool newStatus = respDoc["target_status"] | false;
    
    // Jika ada perubahan status dari dashboard Web
    if (newStatus != targetStatus) {
      Serial.printf("[Kontrol] Status berubah menjadi: %s\n", newStatus ? "ON" : "OFF");
      targetStatus = newStatus;
      updateHardwareState();
    }
  } else {
    Serial.printf("[Heartbeat] Gagal. Kode: %d\n", code);
  }
  http.end();
}

// ============================================================
//  FUNGSI: Kirim Data Sensor (Ke Endpoint 1 Django)
// ============================================================
void sendSensorData() {
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  uint16_t colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  uint16_t lux       = tcs.calculateLux(r, g, b);

  StaticJsonDocument<256> doc;
  doc["raw_light"] = c;
  doc["red"]       = r;
  doc["green"]     = g;
  doc["blue"]      = b;
  doc["temp"]      = colorTemp;
  doc["lux"]       = lux;

  String body; serializeJson(doc, body);

  HTTPClient http;
  http.begin(DATA_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", getAuthHeader());
  http.setTimeout(8000);

  int code = http.POST(body);
  if (code == 201) {
    Serial.println(F("[HTTP] ✅ Data sensor terkirim."));
  } else {
    Serial.printf("[HTTP] ❌ Gagal kirim sensor: %d\n", code);
  }
  http.end();

  // Tampilkan di LCD
  lcd.clear();
  lcd.setCursor(0, 0); lcd.printf("R:%d G:%d B:%d", r, g, b);
  lcd.setCursor(0, 1); lcd.printf("Stat:%s L:%d", targetStatus ? "ON " : "OFF", lux);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // 1. Setup Pin Mode & Default State (Semua OFF di awal)
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(TCS_LED_PIN, OUTPUT);
  pinMode(LED_HIJAU_PIN, OUTPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(RELAY_4_PIN, OUTPUT);
  pinMode(DIMMER_DIM_PIN, OUTPUT);
  pinMode(DIMMER_ZC_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_1_PIN, LOW);
  digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(RELAY_3_PIN, LOW);
  digitalWrite(RELAY_4_PIN, LOW);
  digitalWrite(LED_HIJAU_PIN, LOW);
  digitalWrite(DIMMER_DIM_PIN, LOW);
  digitalWrite(TCS_LED_PIN, HIGH); // LED Sensor nyala terus
  
  currentDimmerPower = 0;
  targetDimmerPower = 0;

  // 2. Setup Dimmer ISR
  dimmerTimer = timerBegin(1000000); 
  timerAttachInterrupt(dimmerTimer, &onDimmerTimer); 
  attachInterrupt(digitalPinToInterrupt(DIMMER_ZC_PIN), zcDetectISR, RISING); 

  // 3. Setup LCD
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("BOOTING SYSTEM..");

  // 4. Mount LittleFS & Load Token
  LittleFS.begin(true);
  loadConfig();

  // 5. Cek Reset Button
  if (digitalRead(TRIGGER_PIN) == LOW) {
    WiFiManager wm; wm.resetSettings();
    LittleFS.remove(CONFIG_FILE);
    lcd.clear(); lcd.print("RESETTING...");
    delay(2000); ESP.restart();
  }

  // 6. Connect WiFi
  startWiFiManager(false);

  // 7. Setup Sensor
  if (!tcs.begin()) {
    lcd.clear(); lcd.print("TCS ERROR!");
    while (1) delay(1000);
  }

  // Langsung kirim heartbeat pertama untuk ambil status mesin dari Server
  sendHeartbeat(); 
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long currentMillis = millis();

  // --- 1. Soft-Start / Soft-Stop AC Dimmer Logic ---
  // Membuat transisi nyala/mati secara perlahan setiap 15ms (1.5 detik dari 0 ke 100%)
  if (currentMillis - lastDimmerUpdate > 15) {
    if (currentDimmerPower < targetDimmerPower) currentDimmerPower++;
    else if (currentDimmerPower > targetDimmerPower) currentDimmerPower--;
    lastDimmerUpdate = currentMillis;
  }

  // --- 2. WiFi Auto Reconnect ---
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(3000);
    return;
  }

  // --- 3. Handle Rutinitas Pengiriman Data Sensor ---
  if (currentMillis - lastDataSent >= SEND_INTERVAL) {
    sendSensorData();
    lastDataSent = currentMillis;
  }

  // --- 4. Handle Rutinitas Sinkronisasi Heartbeat (Menerima Perintah Kontrol) ---
  if (currentMillis - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = currentMillis;
  }

  // --- 5. Manual Reset via Tombol BOOT ---
  if (digitalRead(TRIGGER_PIN) == LOW) {
    delay(3000);
    if (digitalRead(TRIGGER_PIN) == LOW) {
      WiFiManager wm; wm.resetSettings();
      LittleFS.remove(CONFIG_FILE);
      ESP.restart();
    }
  }
}