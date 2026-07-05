/**
 * ============================================================
 *  ESP32 + TCS34725 Color Sensor — IoT Data Sender
 *  Version : 5.3
 *  Features:
 *    - WiFiManager portal SELALU terbuka setiap boot (WiFi tidak
 *      disimpan di flash — wm.resetSettings() dipanggil tiap kali)
 *    - Token API tersimpan di LittleFS (persisten, auto-prefill portal)
 *    - Kirim data ke Django REST API via HTTP POST
 *    - Auto-reconnect dengan exponential backoff
 *    - Jika gagal reconnect 3x -> buka portal WiFiManager lagi
 *    - mDNS: akses portal via http://esp32sensor.local
 *    - Tahan tombol BOOT (GPIO 0) sebentar = buka portal,
 *      tahan 3 detik = hapus token (LittleFS) + restart
 *    - LED iluminasi TCS via GPIO 4
 *    - LED I2C status via GPIO 2
 *    - Aktuator dikontrol PENUH dari WEBSITE (target_status):
 *        Startup  bertahap 5 dtk: Dimmer -> Relay1 -> Relay2 -> Relay3
 *        Shutdown bertahap 5 dtk: LCD -> Sensor -> Relay1 -> Relay2 -> Relay3 -> Dimmer
 *      Relay HIGH=ON/LOW=OFF (GPIO 25/26/27), Dimmer AC (ZC=13, DIM=14)
 *    - Saat baru terhubung: tampil "Website terhubung, siap kirim data",
 *      data sensor DITAHAN sampai website menyalakan proses (tombol ON)
 *    - Dashboard web memicu ON/OFF via /api/device/control/
 *    - Local HTTP Server port 80 untuk device discovery
 *    - Heartbeat ke Django setiap 15 detik (auto-register)
 *    - CORS header agar browser bisa direct fetch
 *    - LED iluminasi TCS via GPIO 4
 *    - LED I2C status via GPIO 2 (biru)
 *    - I2C LCD 16x2 — tampilkan status & data sensor
 *    - I2C eksplisit: SDA=GPIO21, SCL=GPIO22
 * ============================================================
 *
 *  Library yang dibutuhkan (Install via Library Manager):
 *    1. WiFiManager  by tzapu
 *    2. ArduinoJson  by bblanchon
 *    3. Adafruit TCS34725
 *    4. LiquidCrystal I2C by Frank de Brabander
 *    5. LittleFS (built-in ESP32 Arduino Core)
 *    6. ESPmDNS   (built-in)
 *    7. WebServer (built-in)
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "Adafruit_TCS34725.h"

// ============================================================
//  KONFIGURASI
// ============================================================

const char* AP_SSID       = "ESP32_Sensor_Color";
const char* AP_PASSWORD   = "Admin1234";
const char* MDNS_NAME     = "esp32sensor";
const char* SERVER_URL    = "http://192.168.101.6:8000/api/receive-data/";
const char* HEARTBEAT_URL = "http://192.168.101.6:8000/api/device/heartbeat/";
const char* STERILE_URL   = "http://192.168.101.6:8000/api/device/sterile-check/";

#define SDA_PIN                 21
#define SCL_PIN                 22
#define TCS_LED                  4   // LED iluminasi TCS34725
#define I2C_LED                  2   // LED indikator status I2C (biru onboard)

// ── Aktuator mesin (dikontrol penuh dari WEBSITE via target_status) ──
// Relay: HIGH = ON, LOW = OFF
#define RELAY_1_PIN             25   // Relay 1 (mis. Pompa)
#define RELAY_2_PIN             26   // Relay 2 (mis. Chiller)
#define RELAY_3_PIN             27   // Relay 3 (tambahan)
#define DIMMER_ZC_PIN          13   // AC Dimmer — Zero Cross detect (input)
#define DIMMER_DIM_PIN         14   // AC Dimmer — gate trigger (output)
#define DIMMER_TARGET_POWER    100  // Daya ozonator/dimmer saat ON (0–100%)

#define TRIGGER_PIN              0
#define SEND_INTERVAL        10000
#define HEARTBEAT_INTERVAL   15000
#define STEP_INTERVAL         5000  // Jeda 5 detik antar aktuator (startup & shutdown)
#define MAX_RECONNECT_ATTEMPTS   3
#define RECONNECT_BASE_DELAY  3000
#define CONFIG_FILE          "/config.json"

// ============================================================
//  VARIABEL GLOBAL
// ============================================================

char apiToken[150]          = "";
bool shouldSaveConfig       = false;
int  reconnectAttempts      = 0;
bool serverReady            = false;  // True setelah heartbeat pertama sukses ke server
bool welcomeShown           = false;  // True setelah pesan "Website terhubung" ditampilkan
unsigned long lastHeartbeat = 0;
unsigned long lastSensor    = 0;
unsigned long lastLcdUpdate = 0;

// ============================================================
//  STATE MACHINE MESIN (staggered startup/shutdown 5 detik)
//  WEBSITE = sumber kebenaran. IoT hanya menjalankan urutan.
//  Startup  : Dimmer -> Relay1 -> Relay2 -> Relay3  (tiap 5 dtk)
//  Shutdown : LCD notif -> Sensor -> Relay1 -> Relay2 -> Relay3 -> Dimmer
// ============================================================
enum MachineState {
  MACHINE_OFF,        // Idle, semua aktuator mati, sensor tidak mengirim
  MACHINE_STARTING,   // Sekuens menyalakan alat berjalan
  MACHINE_ON,         // Semua alat aktif, sensor mengirim data
  MACHINE_STOPPING    // Sekuens mematikan alat berjalan
};

volatile MachineState machineState = MACHINE_OFF;
int           seqStep      = 0;      // Langkah sekuens saat ini
unsigned long seqStepTime  = 0;      // millis() saat langkah terakhir dieksekusi

// Dimmer AC driver (custom, sama seperti node Ozora)
volatile int  currentDimmerPower = 0;
hw_timer_t*   dimmerTimer = NULL;

LiquidCrystal_I2C* lcd = NULL;
uint8_t lcdAddr = 0;

WebServer localServer(80);

Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);

// ============================================================
//  FUNGSI: LED I2C indikator
// ============================================================
void i2cLedOn()  { digitalWrite(I2C_LED, HIGH); }
void i2cLedOff() { digitalWrite(I2C_LED, LOW);  }

void i2cLedBlink(int times, int ms = 150) {
  for (int i = 0; i < times; i++) {
    i2cLedOn();  delay(ms);
    i2cLedOff(); delay(ms);
  }
}

// ============================================================
//  DIMMER AC — ISR & timer
// ============================================================
void IRAM_ATTR onDimmerTimer() {
  digitalWrite(DIMMER_DIM_PIN, HIGH);
  for (int i = 0; i < 500; i++) { asm volatile ("nop"); }
  digitalWrite(DIMMER_DIM_PIN, LOW);
}

void IRAM_ATTR zcDetectISR() {
  if (currentDimmerPower <= 0)   { digitalWrite(DIMMER_DIM_PIN, LOW);  return; }
  if (currentDimmerPower >= 100) { digitalWrite(DIMMER_DIM_PIN, HIGH); return; }
  uint32_t delayTime = 100 * (100 - currentDimmerPower);
  if (delayTime < 200)  delayTime = 200;
  if (delayTime > 9800) delayTime = 9800;
  timerWrite(dimmerTimer, 0);
  timerAlarm(dimmerTimer, delayTime, false, 0);
}

// Nyalakan seluruh aktuator ke posisi mati (dipakai saat boot)
void allActuatorsOff() {
  currentDimmerPower = 0;
  digitalWrite(DIMMER_DIM_PIN, LOW);
  digitalWrite(RELAY_1_PIN,   LOW);
  digitalWrite(RELAY_2_PIN,   LOW);
  digitalWrite(RELAY_3_PIN,   LOW);
  digitalWrite(TCS_LED,       LOW);
}

// ============================================================
//  KONTROL MESIN — dipicu perubahan target_status dari WEBSITE
//  Frontend yang memutuskan ON/OFF (tombol proc-btn maupun status
//  "Clean Transparent Water"/"Purified"). IoT hanya menjalankan.
// ============================================================
void requestStartup() {
  if (machineState == MACHINE_ON || machineState == MACHINE_STARTING) return;
  Serial.println(F("[Machine] Perintah ON dari website — mulai sekuens startup (5s/alat)."));
  machineState = MACHINE_STARTING;
  seqStep      = 0;
  seqStepTime  = 0;   // 0 = jalankan langkah pertama segera
}

void requestShutdown() {
  if (machineState == MACHINE_OFF || machineState == MACHINE_STOPPING) return;
  Serial.println(F("[Machine] Perintah OFF dari website — mulai sekuens shutdown (5s/alat)."));
  machineState = MACHINE_STOPPING;
  seqStep      = 0;
  seqStepTime  = 0;
}

// True bila mesin sudah sepenuhnya aktif (boleh baca & kirim data sensor)
bool machineIsFullyOn() { return machineState == MACHINE_ON; }

// ============================================================
//  SEQUENCE ENGINE (non-blocking) — dipanggil terus di loop()
//  Tiap langkah dieksekusi berjarak STEP_INTERVAL (5 detik).
// ============================================================
void serviceMachineSequence() {
  unsigned long now = millis();

  // ── STARTUP: Dimmer -> Relay1 -> Relay2 -> Relay3 ──
  if (machineState == MACHINE_STARTING) {
    if (seqStepTime != 0 && (now - seqStepTime < STEP_INTERVAL)) return;
    seqStepTime = now;

    switch (seqStep) {
      case 0:
        Serial.println(F("[Startup] 1/4 Dimmer/Ozonator ON"));
        lcdPrint("MENYALAKAN ALAT", "1/4 Dimmer ON");
        currentDimmerPower = DIMMER_TARGET_POWER;
        i2cLedOn();
        break;
      case 1:
        Serial.println(F("[Startup] 2/4 Relay 1 ON"));
        lcdPrint("MENYALAKAN ALAT", "2/4 Relay 1 ON");
        digitalWrite(RELAY_1_PIN, HIGH);
        break;
      case 2:
        Serial.println(F("[Startup] 3/4 Relay 2 ON"));
        lcdPrint("MENYALAKAN ALAT", "3/4 Relay 2 ON");
        digitalWrite(RELAY_2_PIN, HIGH);
        break;
      case 3:
        Serial.println(F("[Startup] 4/4 Relay 3 ON — sensor aktif"));
        lcdPrint("MENYALAKAN ALAT", "4/4 Relay 3 ON");
        digitalWrite(RELAY_3_PIN, HIGH);
        digitalWrite(TCS_LED,     HIGH);  // Aktifkan iluminasi sensor TCS34725
        break;
      case 4:
        machineState = MACHINE_ON;
        Serial.println(F("[Machine] SEMUA ALAT AKTIF — mulai kirim data sensor."));
        lcdPrint("MESIN AKTIF", "Sensor Berjalan");
        break;
    }
    seqStep++;
    return;
  }

  // ── SHUTDOWN: LCD -> Sensor -> Relay1 -> Relay2 -> Relay3 -> Dimmer ──
  if (machineState == MACHINE_STOPPING) {
    if (seqStepTime != 0 && (now - seqStepTime < STEP_INTERVAL)) return;
    seqStepTime = now;

    switch (seqStep) {
      case 0:
        Serial.println(F("[Shutdown] Air jernih/Purified — mematikan alat bertahap."));
        lcdPrint("AIR JERNIH!", "Mematikan alat..");
        i2cLedBlink(3, 80);
        break;
      case 1:
        Serial.println(F("[Shutdown] 1/5 Sensor TCS34725 OFF"));
        lcdPrint("MEMATIKAN ALAT", "1/5 Sensor OFF");
        digitalWrite(TCS_LED, LOW);
        break;
      case 2:
        Serial.println(F("[Shutdown] 2/5 Relay 1 OFF"));
        lcdPrint("MEMATIKAN ALAT", "2/5 Relay 1 OFF");
        digitalWrite(RELAY_1_PIN, LOW);
        break;
      case 3:
        Serial.println(F("[Shutdown] 3/5 Relay 2 OFF"));
        lcdPrint("MEMATIKAN ALAT", "3/5 Relay 2 OFF");
        digitalWrite(RELAY_2_PIN, LOW);
        break;
      case 4:
        Serial.println(F("[Shutdown] 4/5 Relay 3 OFF"));
        lcdPrint("MEMATIKAN ALAT", "4/5 Relay 3 OFF");
        digitalWrite(RELAY_3_PIN, LOW);
        break;
      case 5:
        Serial.println(F("[Shutdown] 5/5 Dimmer/Ozonator OFF"));
        lcdPrint("MEMATIKAN ALAT", "5/5 Dimmer OFF");
        currentDimmerPower = 0;
        digitalWrite(DIMMER_DIM_PIN, LOW);
        break;
      case 6:
        machineState = MACHINE_OFF;
        i2cLedOff();
        Serial.println(F("[Machine] MESIN OFFLINE — standby menunggu perintah website."));
        lcdPrint("MESIN OFFLINE", "Standby Web...");
        break;
    }
    seqStep++;
    return;
  }
}

// ============================================================
//  FUNGSI: Deteksi LCD I2C (0x27 / 0x3F)
// ============================================================
bool detectLCD() {
  uint8_t addrs[] = {0x27, 0x3F};
  for (int i = 0; i < 2; i++) {
    Wire.beginTransmission(addrs[i]);
    if (Wire.endTransmission() == 0) {
      lcdAddr = addrs[i];
      return true;
    }
  }
  return false;
}

void lcdPrint(const char* line1, const char* line2) {
  if (!lcd) return;
  lcd->clear();
  lcd->setCursor(0, 0); lcd->print(line1);
  lcd->setCursor(0, 1); lcd->print(line2);
}

void lcdPrintf(int row, const char* fmt, ...) {
  if (!lcd) return;
  char buf[17];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, 17, fmt, args);
  va_end(args);
  lcd->setCursor(0, row);
  lcd->print(buf);
}

// ============================================================
//  CUSTOM HTML PORTAL WIFIMANAGER
// ============================================================

const char CUSTOM_HEAD[] PROGMEM = R"rawliteral(
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap');
  :root{--bg:#060912;--surface:#0d1117;--card:#161b27;--border:#21293d;--border2:#2d3a52;--text:#e6edf3;--muted:#8b949e;--accent:#4f8ef7;--accent2:#6366f1;--success:#3fb950;--glow:rgba(79,142,247,0.18)}
  *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Inter',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px 16px 40px;background-image:radial-gradient(ellipse 80% 50% at 50% -20%,rgba(79,142,247,.08) 0%,transparent 70%)}
  .top-nav{width:100%;max-width:460px;display:flex;align-items:center;justify-content:space-between;margin-bottom:28px}
  .brand{display:flex;align-items:center;gap:10px}
  .brand-icon{width:36px;height:36px;border-radius:10px;background:linear-gradient(135deg,var(--accent),var(--accent2));display:flex;align-items:center;justify-content:center;font-size:18px;box-shadow:0 4px 14px var(--glow)}
  .brand-name{font-size:15px;font-weight:700}.brand-sub{font-size:11px;color:var(--muted)}
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
  input[type=submit]:hover{opacity:.88}input[type=submit]:active{transform:scale(.98)}
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
      <div class="brand-sub">ESP32 IoT Device v5.3</div>
    </div>
  </div>
  <div class="status-pill"><span class="pulse-dot"></span>AP Mode</div>
</div>
<div class="card">
  <div class="section-title">Konfigurasi Perangkat</div>
  <p class="section-sub">Hubungkan ke jaringan WiFi dan masukkan API Token untuk mulai mengirim data sensor.</p>
  <div class="chips">
    <span class="chip">&#x1F4E1; TCS34725 RGB</span>
    <span class="chip blue">&#x26A1; ESP32</span>
    <span class="chip">&#x1F504; v5.3</span>
  </div>
  <div class="hint-box">
    <strong>&#x1F511; API Token</strong>
    Paste token dari Dashboard. Prefix <code>Token </code> ditambah otomatis.<br>
    <button class="copy-btn" onclick="copyFormat()">&#x1F4CB; Salin Format</button>
  </div>
  {v}
  <div class="access-info">
    <strong>&#x1F310; Akses Portal Kapan Saja</strong>
    Setelah terhubung WiFi, buka portal via:<br>
    <span class="url-badge">http://esp32sensor.local</span>
    &nbsp;atau IP: <span class="url-badge" id="localip">192.168.4.1</span>
  </div>
</div>
<div class="footer">
  ESP32 Color Sensor v5.3<br>
  <a href="/wifi">Scan WiFi</a> &nbsp;&middot;&nbsp; <a href="/i">Info</a> &nbsp;&middot;&nbsp; <a href="/r">Restart</a>
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
// ============================================================
void setupLocalServer() {
  localServer.on("/info", HTTP_GET, []() {
    localServer.sendHeader("Access-Control-Allow-Origin",  "*");
    localServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    localServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    StaticJsonDocument<300> doc;
    doc["device_id"]   = WiFi.macAddress();
    doc["device_name"] = AP_SSID;
    doc["firmware"]    = "5.3";
    doc["sensor"]      = "TCS34725";
    doc["ip"]          = WiFi.localIP().toString();
    doc["ssid"]        = WiFi.SSID();
    doc["rssi"]        = WiFi.RSSI();
    doc["uptime_ms"]   = millis();

    String resp;
    serializeJson(doc, resp);
    localServer.send(200, "application/json", resp);
    Serial.println(F("[LocalServer] /info requested -> responded"));
  });

  localServer.on("/info", HTTP_OPTIONS, []() {
    localServer.sendHeader("Access-Control-Allow-Origin",  "*");
    localServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    localServer.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    localServer.send(204);
  });

  localServer.on("/", HTTP_GET, []() {
    String html = "<html><body style='font-family:monospace;background:#060912;color:#e6edf3;padding:20px'>";
    html += "<h2>ESP32 Color Sensor v5.3</h2>";
    html += "<p>IP: "     + WiFi.localIP().toString() + "</p>";
    html += "<p>SSID: "   + WiFi.SSID()               + "</p>";
    html += "<p>RSSI: "   + String(WiFi.RSSI())        + " dBm</p>";
    html += "<p>MAC: "    + WiFi.macAddress()           + "</p>";
    html += "<p>Uptime: " + String(millis()/1000)       + "s</p>";
    html += "<p><a href='/info' style='color:#38BDF8'>GET /info (JSON)</a></p>";
    html += "</body></html>";
    localServer.send(200, "text/html", html);
  });

  localServer.begin();
  Serial.println(F("[LocalServer] HTTP server aktif di port 80"));
}

// ============================================================
//  FUNGSI: Heartbeat ke Django (auto-register device)
// ============================================================
void sendHeartbeat() {
  lcdPrintf(0, "Heartbeat...");

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
  doc["firmware"]    = "5.3";

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(HEARTBEAT_URL);
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("Authorization", authHeader);
  http.setTimeout(5000);

  int code = http.POST(body);
  if (code == 200 || code == 201) {
    Serial.println(F("[Heartbeat] Registered ke Django"));
    serverReady = true;  // Server terhubung

    // Saat pertama kali terhubung: TAMPILKAN pesan siap, JANGAN kirim data dulu.
    // Data baru dikirim setelah website menyalakan proses (target_status = true).
    if (!welcomeShown) {
      welcomeShown = true;
      Serial.println(F("[Heartbeat] Website telah berhasil terhubung, siap untuk mengirim data"));
      lcdPrint("Website terhubung", "Siap kirim data");
      delay(1500);
    } else {
      lcdPrintf(0, "Heartbeat OK");
    }

    // Website adalah sumber kebenaran. Baca target_status lalu jalankan
    // sekuens startup/shutdown bertahap (5 detik per alat).
    String resp = http.getString();
    StaticJsonDocument<256> respDoc;
    if (!deserializeJson(respDoc, resp)) {
      bool target = respDoc["target_status"] | false;
      if (target) requestStartup();
      else        requestShutdown();
    }
  } else {
    Serial.printf("[Heartbeat] Status: %d — server belum terhubung, data sensor ditahan.\n", code);
    lcdPrintf(0, "Server OFFLINE");
    lcdPrintf(1, "Menunggu...");
    serverReady = false;  // Jika heartbeat gagal, tahan pengiriman data
  }
  http.end();
}

// ============================================================
//  FUNGSI: Setup WiFiManager
//  - Boot pertama / manual portal: reset WiFi credentials lama
//    agar portal selalu muncul, token tetap dari LittleFS.
//  - Reconnect portal (dari handleReconnect): TIDAK reset
//    credentials, cukup buka portal ulang.
// ============================================================
void startWiFiManager(bool forcePortal = false) {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalTimeout(180);
  wm.setCustomHeadElement(CUSTOM_HEAD);
  wm.setCustomMenuHTML(CUSTOM_BODY);

  // Reset WiFi credentials HANYA pada boot normal atau manual reset,
  // agar portal selalu muncul dan tidak auto-connect ke WiFi lama.
  // Pada reconnect portal (forcePortal=true dari handleReconnect),
  // skip reset supaya tidak loop tak terbatas.
  if (!forcePortal) {
    wm.resetSettings();
    Serial.println(F("[WiFi] WiFi credentials direset — portal akan terbuka."));
  }

  WiFiManagerParameter param_token("token", "API Token (format: Token xxxxx)", apiToken, 148);
  wm.addParameter(&param_token);

  Serial.printf("[WiFi] Membuka portal: SSID=%s\n", AP_SSID);
  lcdPrint("AP Mode", "Config via WiFi");

  bool connected;
  if (forcePortal) {
    connected = wm.startConfigPortal(AP_SSID, AP_PASSWORD);
  } else {
    connected = wm.startConfigPortal(AP_SSID, AP_PASSWORD);
  }

  if (!connected) {
    Serial.println(F("[WiFi] Portal timeout. Restart..."));
    lcdPrint("Timeout!", "Restarting...");
    delay(3000);
    ESP.restart();
  }

  // Ambil token dari form portal; simpan ke flash
  const char* newToken = param_token.getValue();
  if (strlen(newToken) > 0) {
    strlcpy(apiToken, newToken, sizeof(apiToken));
    saveConfig();
  }
  shouldSaveConfig  = false;
  reconnectAttempts = 0;

  Serial.println(F("[WiFi] Terhubung!"));
  Serial.printf("[WiFi] IP      : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WiFi] Gateway : %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("[Token] Aktif  : %.30s...\n\n", apiToken);

  lcdPrint("WiFi Connected", WiFi.localIP().toString().c_str());

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local aktif\n", MDNS_NAME);
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
  Serial.printf("[WiFi] Koneksi putus. Percobaan %d/%d - tunggu %lu ms...\n",
                reconnectAttempts, MAX_RECONNECT_ATTEMPTS, delayMs);
  lcdPrint("WiFi LOST!", "Reconnecting...");
  i2cLedBlink(reconnectAttempts, 200);

  WiFi.reconnect();
  delay(delayMs);

  if (WiFi.status() == WL_CONNECTED) {
    reconnectAttempts = 0;
    i2cLedOn();
    Serial.println(F("[WiFi] Reconnect berhasil!"));
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    lcdPrint("Reconnected!", WiFi.localIP().toString().c_str());
    MDNS.end();
    if (MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[mDNS] mDNS aktif: http://%s.local\n", MDNS_NAME);
    }
    sendHeartbeat();
    return;
  }

  if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
    Serial.println(F("[WiFi] Gagal reconnect. Membuka portal WiFi Manager..."));
    lcdPrint("Open Portal...", "");
    reconnectAttempts = 0;
    startWiFiManager(true);
  }
}

// ============================================================
//  FUNGSI: Kirim data sensor ke Django REST API
//  Setelah kirim, langsung cek steril ke endpoint terpisah.
// ============================================================
void sendSensorData() {
  uint16_t r, g, b, c;
  uint16_t colorTemp, lux;

  lcdPrint("Reading sensor...", "");

  // Nyalakan LED iluminasi TCS + LED I2C saat baca sensor
  digitalWrite(TCS_LED, HIGH);
  i2cLedOn();
  delay(50);

  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux       = tcs.calculateLux(r, g, b);

  digitalWrite(TCS_LED, LOW);
  i2cLedOff();

  Serial.printf("[Sensor] C:%u  R:%u  G:%u  B:%u  |  Lux:%u  Temp:%uK\n",
                c, r, g, b, lux, colorTemp);

  // Tampilkan di LCD
  lcdPrintf(0, "R:%03u G:%03u", r, g);
  lcdPrintf(1, "B:%03u L:%04u", b, lux);

  // ── 1. Kirim data ke endpoint receive-data ──
  {
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

    String authHeader = String(apiToken);
    authHeader.trim();
    if (!authHeader.startsWith("Token ") && !authHeader.startsWith("Bearer ")) {
      authHeader = "Token " + authHeader;
    }

    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Authorization", authHeader);
    http.setTimeout(8000);

    lcdPrintf(0, "Sending data...");
    i2cLedBlink(2, 100);

    int code = http.POST(body);
    if (code > 0) {
      Serial.printf("[HTTP] receive-data status: %d\n", code);
      if (code == 201) { i2cLedOn(); delay(200); i2cLedOff(); }
    } else {
      Serial.printf("[HTTP] Error: %s\n", http.errorToString(code).c_str());
    }
    http.end();
  }

  // ── 2. Kirim ke sterile-check, baca respons mesin ──
  {
    StaticJsonDocument<256> doc;
    doc["device_id"] = WiFi.macAddress();
    doc["raw_light"] = c;
    doc["red"]       = r;
    doc["green"]     = g;
    doc["blue"]      = b;
    doc["lux"]       = lux;

    String body;
    serializeJson(doc, body);

    String authHeader = String(apiToken);
    authHeader.trim();
    if (!authHeader.startsWith("Token ") && !authHeader.startsWith("Bearer ")) {
      authHeader = "Token " + authHeader;
    }

    HTTPClient http;
    http.begin(STERILE_URL);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Authorization", authHeader);
    http.setTimeout(8000);

    int code = http.POST(body);
    if (code == 200) {
      String resp = http.getString();
      Serial.printf("[Sterile] Response: %s\n", resp.c_str());

      StaticJsonDocument<256> respDoc;
      if (!deserializeJson(respDoc, resp)) {
        bool target       = respDoc["target_status"]  | false;
        bool sterile      = respDoc["is_sterile"]     | false;
        int  confirmCount  = respDoc["confirm_count"]  | 0;
        int  confirmNeeded = respDoc["confirm_needed"] | 3;

        // Tampilkan progress di LCD baris ke-1
        if (!sterile && machineIsFullyOn()) {
          lcdPrintf(1, "Jernih:%d/%d", confirmCount, confirmNeeded);
        }

        // Website (via status prediksi / sterile-check) yang memutuskan.
        // Firmware hanya mengeksekusi urutan bertahap sesuai target_status.
        if (target) requestStartup();
        else        requestShutdown();
      }
    } else {
      Serial.printf("[Sterile] HTTP error: %d\n", code);
    }
    http.end();
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  delay(500);

  // LED iluminasi TCS dan LED I2C
  pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_LED, LOW);
  pinMode(I2C_LED, OUTPUT);
  digitalWrite(I2C_LED, LOW);
  // Aktuator mesin — default MATI saat boot (standby, menunggu perintah website)
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(DIMMER_DIM_PIN, OUTPUT);
  pinMode(DIMMER_ZC_PIN, INPUT_PULLUP);
  allActuatorsOff();

  // Inisialisasi driver AC dimmer (timer + zero-cross interrupt)
  dimmerTimer = timerBegin(1000000);
  timerAttachInterrupt(dimmerTimer, &onDimmerTimer);
  attachInterrupt(digitalPinToInterrupt(DIMMER_ZC_PIN), zcDetectISR, RISING);
  currentDimmerPower = 0;

  machineState = MACHINE_OFF;   // Standby sampai website menyalakan proses

  Serial.println(F("\n========================================"));
  Serial.println(F("  ESP32 IoT Color Sensor  v5.3"));
  Serial.println(F("  I2C Pins: SDA=GPIO21, SCL=GPIO22"));
  Serial.println(F("  I2C LED : GPIO2"));
  Serial.println(F("========================================\n"));
  Serial.printf("[Device] MAC: %s\n", WiFi.macAddress().c_str());

  if (!LittleFS.begin(true)) {
    Serial.println(F("[FS] LittleFS mount GAGAL!"));
    return;
  }
  Serial.println(F("[FS] LittleFS OK."));

  loadConfig();

  if (digitalRead(TRIGGER_PIN) == LOW) {
    Serial.println(F("[Reset] Tombol BOOT ditekan - hapus WiFi & Token..."));
    i2cLedBlink(5, 100);
    WiFiManager wm;
    wm.resetSettings();
    LittleFS.remove(CONFIG_FILE);
    Serial.println(F("[Reset] Selesai. Restart dalam 2 detik..."));
    delay(2000);
    ESP.restart();
  }

  // I2C — inisialisasi sebelum LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.printf("[I2C] Wire.begin(SDA=%d, SCL=%d)\n", SDA_PIN, SCL_PIN);

  // Deteksi LCD
  if (detectLCD()) {
    lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcdPrint("ESP32 v5.3", "Booting...");
    Serial.printf("[LCD] 0x%02X detected\n", lcdAddr);
    i2cLedOn();   // LED nyala = I2C/LCD OK
  } else {
    Serial.println(F("[LCD] Not detected"));
    i2cLedBlink(3, 300);  // Blink 3x = LCD tidak ditemukan
  }

  delay(500);

  startWiFiManager(false);
  setupLocalServer();
  sendHeartbeat();
  lastHeartbeat = millis();

  if (!tcs.begin()) {
    Serial.println(F("[Sensor] TCS34725 tidak terdeteksi! Cek kabel I2C."));
    lcdPrint("Sensor ERROR", "Check I2C wiring");
    i2cLedBlink(10, 100);
    while (1) delay(1000);
  }
  Serial.println(F("[Sensor] TCS34725 siap."));
  lcdPrint("Sensor Ready", "Starting...");
  i2cLedBlink(2, 200);
  i2cLedOn();

  Serial.println(F("\n----------------------------------------"));
  Serial.println(F("  Sistem siap. Loop dimulai..."));
  Serial.printf ("  Discovery URL: http://%s/info\n", WiFi.localIP().toString().c_str());
  Serial.println(F("----------------------------------------\n"));
  delay(1000);
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Cek tombol BOOT untuk reset
  if (digitalRead(TRIGGER_PIN) == LOW) {
    delay(50);
    if (digitalRead(TRIGGER_PIN) == LOW) {
      Serial.println(F("[Reset] Tombol BOOT ditekan - buka portal WiFi..."));
      lcdPrint("Hold 3s...", "to full reset");
      delay(3000);
      if (digitalRead(TRIGGER_PIN) == LOW) {
        Serial.println(F("[Reset] Reset penuh WiFi + Token..."));
        i2cLedBlink(5, 100);
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

  // Jalankan sekuens startup/shutdown bertahap (non-blocking, 5 dtk/alat)
  serviceMachineSequence();

  if (WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();

    localServer.handleClient();

    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
      sendHeartbeat();
      lastHeartbeat = now;
    }

    // Hanya kirim data sensor kalau:
    // 1. Mesin sudah SEPENUHNYA aktif (sekuens startup selesai)
    // 2. Server sudah terkonfirmasi terhubung (heartbeat pernah sukses)
    if (machineIsFullyOn() && serverReady && (now - lastSensor >= SEND_INTERVAL)) {
      sendSensorData();
      lastSensor = now;
    }

    // Selama standby (mesin OFF & server ready): tampilkan info menunggu perintah web
    if (serverReady && machineState == MACHINE_OFF && (now - lastSensor >= SEND_INTERVAL)) {
      lcdPrint("Standby...", "Menunggu Web ON");
      lastSensor = now;
    }

    // Tampilkan status menunggu server di LCD jika belum ready
    if (!serverReady && (now - lastSensor >= SEND_INTERVAL)) {
      Serial.println(F("[Data] Ditahan — server belum terhubung. Menunggu heartbeat sukses..."));
      lcdPrint("Menunggu server", "Cek koneksi...");
      lastSensor = now; // reset agar tidak spam log
    }

  } else {
    handleReconnect();
  }
}
