/**
 * ============================================================
 *  ESP32 + TCS34725 Color Sensor — IoT Data Sender
 *  Author  : (your name)
 *  Version : 2.0
 *  Features:
 *    - WiFiManager dengan halaman konfigurasi custom HTML
 *    - Token API tersimpan di LittleFS (persisten)
 *    - Kirim data ke Django REST API via HTTP POST
 *    - Tahan tombol BOOT (GPIO 0) untuk reset WiFi + Token
 * ============================================================
 *
 *  Library yang dibutuhkan (Install via Library Manager):
 *    1. WiFiManager  by tzapu     https://github.com/tzapu/WiFiManager
 *    2. ArduinoJson  by bblanchon https://github.com/bblanchon/ArduinoJson
 *    3. Adafruit TCS34725         https://github.com/adafruit/Adafruit_TCS34725
 *    4. Adafruit BusIO            (dependensi otomatis)
 *    5. LittleFS                  (built-in ESP32 Arduino Core)
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Wire.h>
#include "Adafruit_TCS34725.h"

// ============================================================
//  KONFIGURASI — Sesuaikan bagian ini
// ============================================================

// Nama & password hotspot ESP32 saat mode konfigurasi
const char* AP_SSID     = "ESP32_Sensor_Color";
const char* AP_PASSWORD = "Admin1234";          // Minimal 8 karakter

// URL endpoint Django REST API
const char* SERVER_URL  = "http://ozora.b4its.tech/api/receive-data/";

// Pin tombol reset (GPIO 0 = tombol BOOT bawaan ESP32)
#define TRIGGER_PIN     0

// Interval pengiriman data (milidetik)
#define SEND_INTERVAL   10000   // 10 detik

// Path file konfigurasi di LittleFS
#define CONFIG_FILE     "/config.json"

// ============================================================
//  VARIABEL GLOBAL
// ============================================================

// Buffer token — format: "Token <your_token_here>"
char apiToken[150] = "";

// Flag: perlu simpan konfigurasi baru ke flash
bool shouldSaveConfig = false;

// Inisialisasi sensor TCS34725 (50 ms integrasi, gain 4x)
Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);

// ============================================================
//  CUSTOM HTML UNTUK PORTAL WIFIMANAGER
//  Akan ditampilkan di browser ketika konek ke hotspot ESP32
// ============================================================

// Injected ke <head> halaman portal
const char CUSTOM_HEAD[] PROGMEM = R"rawliteral(
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    font-family: 'Inter', sans-serif;
    background: #0f172a;
    color: #e2e8f0;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 24px 16px;
  }

  /* Header badge */
  .device-badge {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 99px;
    padding: 6px 16px;
    font-size: 13px;
    color: #94a3b8;
    margin-bottom: 20px;
  }
  .device-badge .dot {
    width: 8px; height: 8px;
    background: #22c55e;
    border-radius: 50%;
    animation: pulse 1.5s infinite;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50%       { opacity: 0.4; }
  }

  /* Card utama */
  .wm-card {
    width: 100%;
    max-width: 420px;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 16px;
    padding: 28px 24px;
    box-shadow: 0 20px 60px rgba(0,0,0,.4);
  }

  .wm-card h1 {
    font-size: 20px;
    font-weight: 700;
    color: #f1f5f9;
    margin-bottom: 4px;
    letter-spacing: -0.3px;
  }
  .wm-card .subtitle {
    font-size: 13px;
    color: #64748b;
    margin-bottom: 24px;
  }

  /* Label & input */
  label {
    display: block;
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.6px;
    color: #94a3b8;
    margin-bottom: 6px;
  }
  input[type=text],
  input[type=password] {
    width: 100%;
    background: #0f172a;
    border: 1px solid #334155;
    border-radius: 10px;
    padding: 11px 14px;
    font-size: 14px;
    color: #e2e8f0;
    outline: none;
    transition: border-color .2s, box-shadow .2s;
    margin-bottom: 16px;
    font-family: 'Inter', monospace;
  }
  input[type=text]:focus,
  input[type=password]:focus {
    border-color: #3b82f6;
    box-shadow: 0 0 0 3px rgba(59,130,246,.2);
  }

  /* Token hint box */
  .token-hint {
    background: #0f172a;
    border: 1px solid #1d4ed8;
    border-left: 3px solid #3b82f6;
    border-radius: 8px;
    padding: 10px 14px;
    font-size: 12px;
    color: #93c5fd;
    margin-bottom: 20px;
    line-height: 1.6;
  }
  .token-hint strong { color: #bfdbfe; display: block; margin-bottom: 2px; }
  .token-hint code {
    background: #1e3a5f;
    border-radius: 4px;
    padding: 1px 5px;
    font-family: monospace;
    letter-spacing: .3px;
  }

  /* Tombol */
  input[type=submit],
  button {
    width: 100%;
    background: #3b82f6;
    border: none;
    border-radius: 10px;
    padding: 13px;
    font-size: 15px;
    font-weight: 600;
    color: #fff;
    cursor: pointer;
    transition: background .2s, transform .1s;
    margin-top: 4px;
    letter-spacing: 0.2px;
  }
  input[type=submit]:hover, button:hover { background: #2563eb; }
  input[type=submit]:active, button:active { transform: scale(.98); }

  /* Divider */
  hr { border: none; border-top: 1px solid #334155; margin: 20px 0; }

  /* Footer */
  .wm-footer {
    margin-top: 20px;
    font-size: 11px;
    color: #475569;
    text-align: center;
  }
</style>
)rawliteral";

// Injected sebelum </body> — konten ekstra di bawah form
const char CUSTOM_FOOTER[] PROGMEM = R"rawliteral(
<div class="wm-footer">
  ESP32 IoT Color Sensor &nbsp;·&nbsp; v2.0<br>
  Setelah simpan, perangkat akan restart otomatis.
</div>
)rawliteral";

// Mengganti seluruh body halaman utama portal
const char CUSTOM_BODY[] PROGMEM = R"rawliteral(
<div class="device-badge">
  <span class="dot"></span>
  ESP32 &mdash; Mode Konfigurasi
</div>

<div class="wm-card">
  <h1>🎨 Konfigurasi Sensor</h1>
  <p class="subtitle">Hubungkan ke WiFi dan masukkan API Token Anda</p>

  <div class="token-hint">
    <strong>📋 Format Token</strong>
    Salin token dari Dashboard, lalu tambahkan prefix:<br>
    <code>Token &lt;paste_token_anda_disini&gt;</code>
  </div>

  {v}
</div>
)rawliteral";

// ============================================================
//  CALLBACK — Dipanggil WiFiManager saat config perlu disimpan
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
  if (!f) {
    Serial.println(F("[Config] Gagal membuka file config!"));
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("[Config] JSON parse error: %s\n", err.c_str());
    return;
  }

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
  if (!f) {
    Serial.println(F("[Config] Gagal membuka file untuk ditulis!"));
    return;
  }

  serializeJson(doc, f);
  f.close();
  Serial.println(F("[Config] Konfigurasi tersimpan ke flash."));
}

// ============================================================
//  FUNGSI: Kirim data sensor ke Django REST API
// ============================================================
void sendSensorData() {
  uint16_t r, g, b, c;
  uint16_t colorTemp, lux;

  // Baca sensor
  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c); // Lebih akurat
  lux       = tcs.calculateLux(r, g, b);

  // Debug ke Serial
  Serial.printf("[Sensor] C:%u R:%u G:%u B:%u | Lux:%u | Temp:%uK\n",
                c, r, g, b, lux, colorTemp);

  // Susun JSON — field name HARUS cocok dengan serializer Django
  StaticJsonDocument<256> doc;
  doc["raw_light"] = c;          // = clear channel
  doc["red"]       = r;
  doc["green"]     = g;
  doc["blue"]      = b;
  doc["temp"]      = colorTemp;
  doc["lux"]       = lux;

  String body;
  serializeJson(doc, body);

  // Kirim HTTP POST
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", apiToken);
  http.setTimeout(8000);

  Serial.printf("[HTTP] POST ke %s\n", SERVER_URL);
  int code = http.POST(body);

  if (code > 0) {
    String resp = http.getString();
    Serial.printf("[HTTP] Kode: %d | Response: %s\n", code, resp.c_str());

    if (code == 201) {
      Serial.println(F("[HTTP] ✅ Data berhasil dikirim!"));
    } else if (code == 400) {
      Serial.println(F("[HTTP] ⚠  Bad Request — periksa field / token!"));
    } else if (code == 401 || code == 403) {
      Serial.println(F("[HTTP] 🔒 Unauthorized — token salah atau expired!"));
    }
  } else {
    Serial.printf("[HTTP] ❌ Error: %s\n", http.errorToString(code).c_str());
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

  Serial.println(F("\n\n========================================"));
  Serial.println(F("   ESP32 IoT Color Sensor  v2.0"));
  Serial.println(F("========================================\n"));

  // 1. Mount filesystem
  if (!LittleFS.begin(true)) {
    Serial.println(F("[FS] LittleFS mount GAGAL! Cek partisi flash."));
    return;
  }
  Serial.println(F("[FS] LittleFS OK."));

  // 2. Load token dari flash
  loadConfig();

  // 3. Cek tombol reset (tahan BOOT saat power-on)
  if (digitalRead(TRIGGER_PIN) == LOW) {
    Serial.println(F("[Reset] Tombol BOOT ditekan — hapus WiFi & Token..."));
    WiFiManager wm;
    wm.resetSettings();
    LittleFS.remove(CONFIG_FILE);
    Serial.println(F("[Reset] Selesai. Restart dalam 2 detik..."));
    delay(2000);
    ESP.restart();
  }

  // 4. Setup WiFiManager
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalTimeout(180); // 3 menit timeout portal

  // Pasang custom HTML
  wm.setCustomHeadElement(CUSTOM_HEAD);
  wm.setCustomMenuHTML(CUSTOM_BODY);   // Ganti body halaman utama
  // Footer (uncomment jika WiFiManager versi Anda support setCustomFooterElement)
  // wm.setCustomFooterElement(CUSTOM_FOOTER);

  // Parameter custom: API Token
  WiFiManagerParameter param_token(
    "token",                                          // ID (nama field di form)
    "API Token (format: Token xxxxx)",                // Label
    apiToken,                                         // Nilai default
    148                                               // Max panjang
  );
  wm.addParameter(&param_token);

  // 5. Coba konek / tampilkan portal
  Serial.printf("[WiFi] Mencoba konek... Hotspot: %s\n", AP_SSID);

  if (!wm.autoConnect(AP_SSID, AP_PASSWORD)) {
    Serial.println(F("[WiFi] Gagal konek & timeout. Restart..."));
    delay(3000);
    ESP.restart();
  }

  // 6. WiFi terhubung — ambil nilai token dari form (jika baru diisi)
  strlcpy(apiToken, param_token.getValue(), sizeof(apiToken));

  // Simpan jika ada config baru
  if (shouldSaveConfig) {
    saveConfig();
  }

  Serial.println(F("[WiFi] ✅ Terhubung ke WiFi!"));
  Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[Token] Aktif: %.30s...\n\n", apiToken);

  // 7. Inisialisasi sensor TCS34725
  if (!tcs.begin()) {
    Serial.println(F("[Sensor] ❌ TCS34725 tidak terdeteksi! Cek kabel I2C."));
    while (1) delay(1000);
  }
  Serial.println(F("[Sensor] ✅ TCS34725 OK."));
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    sendSensorData();
  } else {
    Serial.println(F("[WiFi] Koneksi terputus, mencoba reconnect..."));
    WiFi.reconnect();
    delay(5000); // Tunggu sebelum coba lagi
    return;
  }

  delay(SEND_INTERVAL);
}