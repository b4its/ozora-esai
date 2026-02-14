#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <LittleFS.h>
#include <Wire.h>
#include "Adafruit_TCS34725.h"

// ================= KONFIGURASI PENGGUNA =================
const char* apSSID = "ESP32_Sensor_Color"; // Nama Hotspot saat konfigurasi
const char* apPASS = "Admin1234";          // Password Hotspot (Wajib 8 char)
const char* serverName = "http://domain-vps-kamu.com/api/receive-data/";

// Pin untuk Reset WiFi (Gunakan tombol BOOT di ESP32 biasanya GPIO 0)
#define TRIGGER_PIN 0 

// Variable Token
char apiToken[128] = "Token "; // Buffer diperbesar untuk keamanan

// Flag config
bool shouldSaveConfig = false;

// Inisialisasi Sensor
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

// Callback notifikasi simpan config
void saveConfigCallback() {
  Serial.println(F("New configuration detected, preparing storage..."));
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  
  // Tunggu serial sebentar
  delay(1000);
  Serial.println(F("\n\n--- ESP32 IoT System Start ---"));

  // --- 1. Mount File System ---
  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS Mount Failed"));
    return;
  }

  // --- 2. Load Config dari File ---
  if (LittleFS.exists("/config.json")) {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);

      StaticJsonDocument<512> json;
      DeserializationError error = deserializeJson(json, buf.get());

      if (!error) {
        if(json.containsKey("apiToken")) strcpy(apiToken, json["apiToken"]);
        Serial.println(F("Tokens are loaded from memory."));
      }
      configFile.close();
    }
  }

  // --- 3. Cek Tombol Reset (Force Config Portal) ---
  // Tahan tombol BOOT saat menyalakan/restart untuk mereset WiFi
  if (digitalRead(TRIGGER_PIN) == LOW) {
    Serial.println(F("Reset button pressed! Deleting WiFi settings..."));
    WiFiManager wm;
    wm.resetSettings();
    // Hapus juga file config jika perlu benar-benar bersih
    // LittleFS.remove("/config.json"); 
    Serial.println(F("Reset complete. Restarting..."));
    delay(1000);
    ESP.restart();
  }

  // --- 4. WiFiManager Setup ---
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  
  // Set Timeout (jika tidak ada yang konek dalam 3 menit, dia akan mencoba reconnect ulang)
  wm.setConfigPortalTimeout(180); 

  // Custom Parameter (Token)
  // Argumen: ID, Label, Default Value, Length
  WiFiManagerParameter custom_api_token("token", "Enter Full Token (eg: Token abc12345)", apiToken, 120);
  wm.addParameter(&custom_api_token);

  // Buat Hotspot dengan Password
  // IP Default biasanya 192.168.4.1
  Serial.print(F("Trying to connect... If it fails, Hotspot will be active: "));
  Serial.println(apSSID);

  pinMode(0, INPUT_PULLUP);
  if (digitalRead(0) == LOW) { // Jika tombol ditekan saat menyala
      Serial.println("Resetting WiFi & Token...");
      wm.resetSettings(); // Hapus WiFi
      LittleFS.format();    // Hapus Token
      delay(1000);
      ESP.restart();
  }
  
  if (!wm.autoConnect(apSSID, apPASS)) {
    Serial.println(F("Failed to connect and timeout. Restarting..."));
    delay(3000);
    ESP.restart();
  }

  // --- 5. Simpan Config Baru (Jika ada) ---
  strcpy(apiToken, custom_api_token.getValue());

  if (shouldSaveConfig) {
    Serial.println(F("Saved configuration."));
    StaticJsonDocument<512> json;
    json["apiToken"] = apiToken;

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("failed to open config file for writing"));
    } else {
      serializeJson(json, configFile);
      configFile.close();
      Serial.println(F("Saved configuration."));
    }
  }

  Serial.println(F("Connect to WiFi!"));
  Serial.print(F("IP Address: "));
  Serial.println(WiFi.localIP());

  // Init Sensor
  if (!tcs.begin()) {
    Serial.println(F("Error: TCS34725 sensor not found! Check I2C wiring."));
    while (1); // Halt
  }
}

void loop() {
  // Cek Status WiFi
  if (WiFi.status() == WL_CONNECTED) {
    
    // Baca Sensor
    uint16_t r, g, b, c, colorTemp, lux;
    tcs.getRawData(&r, &g, &b, &c);
    colorTemp = tcs.calculateColorTemperature(r, g, b);
    lux = tcs.calculateLux(r, g, b);

    // Buat JSON Payload
    StaticJsonDocument<256> doc;
    doc["red"] = r;
    doc["green"] = g;
    doc["blue"] = b;
    doc["clear"] = c;
    doc["temp"] = colorTemp;
    doc["lux"] = lux;
    
    // Tambahkan info device (opsional, agar backend tahu ini alat yang mana)
    doc["device_ip"] = WiFi.localIP().toString(); 

    String requestBody;
    serializeJson(doc, requestBody);

    // Kirim HTTP POST
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    
    // Header Authorization
    // Pastikan user memasukkan format yang benar di portal atau format disini
    // Disini diasumsikan user memasukkan string lengkap "Token xxxxx" atau kita kirim raw
    http.addHeader("Authorization", apiToken); 
    
    // Timeout agar tidak blocking terlalu lama jika server down
    http.setTimeout(5000); 

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      Serial.printf("HTTP Code: %d | Response: ", httpResponseCode);
      Serial.println(http.getString());
    } else {
      Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
  } else {
    Serial.println(F("WiFi Disconnected. Try reconnecting..."));
    WiFi.reconnect(); // Coba paksa reconnect
  }

  // Delay pengiriman (sesuaikan kebutuhan, misal 10 detik)
  delay(10000); 
}