#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <LiquidCrystal_I2C.h>
#include "Adafruit_TCS34725.h"

// ==========================================
// KONFIGURASI JARINGAN & API
// ==========================================
const char* AP_SSID = "Ozora_V2_Config";
const char* AP_PASSWORD = "Admin1234";
const char* MDNS_NAME = "esp32ozora";
const char* SERVER_URL_DATA = "https://ozora.b4its.tech/api/receive-data/";
const char* SERVER_URL_BEAT = "https://ozora.b4its.tech/api/device/heartbeat/";

#define TRIGGER_PIN 0
#define SEND_INTERVAL 10000
#define HEARTBEAT_INTERVAL 15000
#define CONFIG_FILE "/config.json"

char apiToken[150] = "";
bool shouldSaveConfig = false;
String macAddress = "";

unsigned long lastSendTime = 0;
unsigned long lastHeartbeatTime = 0;

// Variabel Kontrol Target Status
bool currentHardwareState = true; // Setup awal menyala

// ==========================================
// DEFINISI PIN HARDWARE
// ==========================================
#define TCS_LED_PIN 4
#define LED_HIJAU_PIN 19
#define RELAY_1_PIN 25  // Pompa
#define RELAY_2_PIN 26  // Chiller
#define RELAY_3_PIN 27  // Relay Tambahan 1
#define RELAY_4_PIN 32  // Relay Tambahan 2
#define DIMMER_ZC_PIN 13
#define DIMMER_DIM_PIN 14

LiquidCrystal_I2C lcd(0x27, 16, 2); 
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

// ==========================================
// DRIVER AC DIMMER
// ==========================================
volatile int currentDimmerPower = 100;
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

// ==========================================
// FUNGSI SEKUENSI HARDWARE
// ==========================================
void runStartupSequence() {
    Serial.println("[SYSTEM] Menyalakan Mesin...");
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("SYSTEM NYALA!   ");
    
    digitalWrite(RELAY_1_PIN, HIGH);
    digitalWrite(RELAY_2_PIN, HIGH);
    digitalWrite(RELAY_3_PIN, HIGH);
    digitalWrite(RELAY_4_PIN, HIGH);
    digitalWrite(LED_HIJAU_PIN, HIGH); 
    digitalWrite(TCS_LED_PIN, HIGH);  
    
    for (int p = 0; p <= 100; p += 2) {
        currentDimmerPower = p;
        delay(20);
    }
}

void runShutdownSequence() {
    Serial.println("[SYSTEM] Sekuensi Mematikan Mesin...");
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("SHUTDOWN MESIN: ");
    
    int jedaAman = 800;

    for (int p = 100; p >= 0; p -= 2) {
        currentDimmerPower = p;
        delay(20);
    }
    lcd.setCursor(0, 1); lcd.print("Dimmer 0%       "); delay(jedaAman);
    digitalWrite(RELAY_1_PIN, LOW); lcd.setCursor(0, 1); lcd.print("Pompa OFF       "); delay(jedaAman);
    digitalWrite(RELAY_2_PIN, LOW); lcd.setCursor(0, 1); lcd.print("Chiller OFF     "); delay(jedaAman);
    digitalWrite(RELAY_3_PIN, LOW); lcd.setCursor(0, 1); lcd.print("Relay 3 OFF     "); delay(jedaAman);
    digitalWrite(RELAY_4_PIN, LOW); lcd.setCursor(0, 1); lcd.print("Relay 4 OFF     "); delay(jedaAman);
    digitalWrite(LED_HIJAU_PIN, LOW); lcd.setCursor(0, 1); lcd.print("Sistem OFF      "); delay(1000);
    
    lcd.clear(); lcd.print("STANDBY...");
}

// ==========================================
// SISTEM WIFIMANAGER & LITTLEFS
// ==========================================
void saveConfigCallback() {
    shouldSaveConfig = true;
}

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

void startWiFiManager(bool forcePortal = false) {
    WiFiManager wm;
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setConfigPortalTimeout(180);
    
    WiFiManagerParameter param_token("token", "API Token", apiToken, 148);
    wm.addParameter(&param_token);

    lcd.clear(); lcd.print("CONNECTING WIFI");

    bool connected = forcePortal ? wm.startConfigPortal(AP_SSID, AP_PASSWORD) : wm.autoConnect(AP_SSID, AP_PASSWORD);

    if (!connected) {
        lcd.setCursor(0, 1); lcd.print("TIMEOUT! RESTART");
        delay(3000); ESP.restart();
    }

    strlcpy(apiToken, param_token.getValue(), sizeof(apiToken));
    if (shouldSaveConfig) {
        saveConfig();
        shouldSaveConfig = false;
    }

    macAddress = WiFi.macAddress();
    MDNS.begin(MDNS_NAME);

    lcd.clear(); lcd.print("WIFI CONNECTED!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    delay(2000); lcd.clear();
}

// ==========================================
// HTTP REQUESTS (HEARTBEAT & SENSOR DATA)
// ==========================================
String getAuthHeader() {
    String auth = String(apiToken);
    auth.trim();
    if (!auth.startsWith("Token ") && !auth.startsWith("Bearer ")) {
        auth = "Token " + auth;
    }
    return auth;
}

void sendHeartbeat() {
    if (WiFi.status() != WL_CONNECTED) return;

    StaticJsonDocument<256> doc;
    doc["device_id"] = macAddress;
    doc["device_name"] = "Ozora V2 Node";
    doc["ip_local"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["firmware"] = "v3.1-ozora";

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.begin(SERVER_URL_BEAT);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", getAuthHeader());
    http.setTimeout(5000);
    
    int code = http.POST(body);
    if(code > 0) {
        String response = http.getString();
        StaticJsonDocument<512> respDoc;
        if (!deserializeJson(respDoc, response)) {
            if (respDoc.containsKey("target_status")) {
                bool targetStatus = respDoc["target_status"];
                
                // Eksekusi jika terjadi perubahan status dari web
                if (targetStatus == false && currentHardwareState == true) {
                    Serial.println("[KONTROL] Perintah OFF dari Web!");
                    runShutdownSequence();
                    currentHardwareState = false;
                } 
                else if (targetStatus == true && currentHardwareState == false) {
                    Serial.println("[KONTROL] Perintah ON dari Web!");
                    runStartupSequence();
                    currentHardwareState = true;
                }
            }
        }
    }
    http.end();
}

void sendSensorData(uint16_t r, uint16_t g, uint16_t b, uint16_t c, uint16_t lux, uint16_t temp) {
    if (WiFi.status() != WL_CONNECTED) return;
    StaticJsonDocument<256> doc;
    doc["raw_light"] = c; doc["red"] = r; doc["green"] = g;
    doc["blue"] = b; doc["lux"] = lux; doc["temp"] = temp;

    String body; serializeJson(doc, body);

    HTTPClient http;
    http.begin(SERVER_URL_DATA);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", getAuthHeader());
    http.POST(body);
    http.end();
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    pinMode(TRIGGER_PIN, INPUT_PULLUP);

    // 1. INIT HARDWARE & RELAY
    pinMode(TCS_LED_PIN, OUTPUT); pinMode(LED_HIJAU_PIN, OUTPUT);
    pinMode(RELAY_1_PIN, OUTPUT); pinMode(RELAY_2_PIN, OUTPUT);
    pinMode(RELAY_3_PIN, OUTPUT); pinMode(RELAY_4_PIN, OUTPUT);
    pinMode(DIMMER_DIM_PIN, OUTPUT); pinMode(DIMMER_ZC_PIN, INPUT_PULLUP);

    // 2. INIT I2C & LCD
    lcd.init(); lcd.backlight();
    
    // 3. INIT DIMMER TIMER
    dimmerTimer = timerBegin(1000000); 
    timerAttachInterrupt(dimmerTimer, &onDimmerTimer); 
    attachInterrupt(digitalPinToInterrupt(DIMMER_ZC_PIN), zcDetectISR, RISING); 

    // 4. NYALAKAN SISTEM DULUAN (Kondisi Awal)
    runStartupSequence();

    // 5. INIT SENSOR
    if (!tcs.begin()) {
        lcd.clear(); lcd.print("TCS ERROR!");
        while (1); 
    }

    // 6. NETWORK INIT (Aman karena mesin sudah diposisi ON dan stabil)
    LittleFS.begin(true);
    loadConfig();
    
    if (digitalRead(TRIGGER_PIN) == LOW) {
        WiFiManager wm; wm.resetSettings();
        LittleFS.remove(CONFIG_FILE); ESP.restart();
    }
    
    startWiFiManager(false);
}

void loop() {
    if (digitalRead(TRIGGER_PIN) == LOW) {
        delay(50);
        if (digitalRead(TRIGGER_PIN) == LOW) {
            lcd.clear(); lcd.print("PORTAL MODE");
            startWiFiManager(true); return;
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect(); delay(3000); return;
    }

    unsigned long currentMillis = millis();

    // TASK 1: Sinkronisasi Kontrol Mesin (Heartbeat)
    if (currentMillis - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
        lastHeartbeatTime = currentMillis;
        sendHeartbeat();
    }

    // TASK 2: Pembacaan & Pengiriman Data Sensor
    if (currentMillis - lastSendTime >= SEND_INTERVAL) {
        lastSendTime = currentMillis;

        uint16_t r, g, b, c, colorTemp, lux;
        tcs.getRawData(&r, &g, &b, &c);
        colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
        lux = tcs.calculateLux(r, g, b);

        if(!currentHardwareState) {
             lcd.setCursor(0, 0); lcd.printf("R:%-3d G:%-3d B:%-3d", r, g, b);
             lcd.setCursor(0, 1); lcd.printf("C:%-3d Lx:%-3d  ", c, lux);
        }

        sendSensorData(r, g, b, c, lux, colorTemp);
    }
}