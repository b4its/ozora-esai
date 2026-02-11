#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "Adafruit_TCS34725.h"

// ================= KONFIGURASI =================
// Ganti dengan URL endpoint Django kamu
// Pastikan tidak menggunakan 'localhost', gunakan IP Public VPS atau Domain
const char* serverName = "http://domain-vps-kamu.com/api/receive-data/";

// PENTING: Token User dari Django
// Format string harus: "Token <paste_token_disini>"
// Contoh: "Token 9944b09199c62bcf9418ad846dd0e4bbdfc6ee4b"
const char* apiToken = "Token 9944b09199c62bcf9418ad846dd0e4bbdfc6ee4b"; 

// ================= HARDWARE =================
// Inisialisasi Sensor TCS34725
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

void setup() {
  Serial.begin(115200);
  
  // ------------------------------------------------
  // 1. WiFiManager Setup (Mode Hotspot -> WiFi)
  // ------------------------------------------------
  WiFiManager wm;
  
  // Reset settingan wifi yang tersimpan (Hapus comment di bawah ini untuk testing ulang dari nol)
  // wm.resetSettings();

  // Ini membuat ESP32 jadi Hotspot bernama "ESP32_Config_Portal"
  // Jika user connect ke sini, akan muncul pop-up untuk pilih WiFi rumah/kantor
  bool res;
  res = wm.autoConnect("ESP32_Config_Portal"); // password opsional, misal: "12345678"

  if (!res) {
    Serial.println("Gagal connect atau timeout");
    // Restart supaya mencoba lagi
    ESP.restart();
  } 
  
  Serial.println("");
  Serial.println("Berhasil terhubung ke WiFi!");
  Serial.print("IP Address ESP32: ");
  Serial.println(WiFi.localIP());

  // ------------------------------------------------
  // 2. Cek Sensor
  // ------------------------------------------------
  if (!tcs.begin()) {
    Serial.println("Error: Sensor TCS34725 tidak ditemukan! Cek kabel.");
    while (1); // Stop di sini jika sensor rusak
  }
}

void loop() {
  // Cek apakah WiFi masih terhubung sebelum kirim data
  if (WiFi.status() == WL_CONNECTED) {
    
    // --- Ambil Data Sensor ---
    uint16_t r, g, b, c, colorTemp, lux;
    tcs.getRawData(&r, &g, &b, &c);
    colorTemp = tcs.calculateColorTemperature(r, g, b);
    lux = tcs.calculateLux(r, g, b);

    // --- Format Data ke JSON ---
    // Pastikan key ("red", "green", dll) SAMA PERSIS dengan field di Serializer Django
    StaticJsonDocument<200> doc;
    doc["red"] = r;
    doc["green"] = g;
    doc["blue"] = b;
    // doc["clear"] = c; // Hapus ini jika di Model Django tidak ada field 'clear'
    doc["temp"] = colorTemp;
    doc["lux"] = lux;

    String requestBody;
    serializeJson(doc, requestBody);

    // --- Debugging di Serial Monitor ---
    Serial.println("Mengirim data: " + requestBody);

    // --- Kirim ke Django ---
    HTTPClient http;
    http.begin(serverName);

    // Header Wajib untuk Django REST Framework
    http.addHeader("Content-Type", "application/json");
    
    // Header Autentikasi (Agar Django tahu ini User siapa)
    http.addHeader("Authorization", apiToken);

    // Eksekusi POST
    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Sukses! Kode Respon: ");
      Serial.println(httpResponseCode);
      Serial.println("Balasan Server: " + response);
    } else {
      Serial.print("Error saat mengirim POST: ");
      Serial.println(httpResponseCode);
    }

    // Bebaskan resource
    http.end();

  } else {
    Serial.println("WiFi Terputus! Mencoba reconnect...");
    WiFi.reconnect();
  }

  // Kirim data setiap 5 detik
  delay(5000);
}