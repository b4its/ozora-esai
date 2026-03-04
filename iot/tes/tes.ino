#include <Wire.h>
#include "Adafruit_TCS34725.h"

// --- DEKLARASI PIN ---
// P2 di shield sama dengan GPIO 2 di ESP32
const int pinLedTCS = 2; 

// Inisialisasi sensor TCS34725
// Waktu integrasi 50ms dan Gain 4x
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Memulai Tes Sensor TCS34725 ---");

  // 1. ATUR LAMPU NYALA TERUS
  pinMode(pinLedTCS, OUTPUT);
  digitalWrite(pinLedTCS, HIGH); 
  Serial.println("Status: Lampu Sensor Dinyalakan (ON Terus)");

  // 2. CEK KONEKSI SENSOR
  if (tcs.begin()) {
    Serial.println("Mantap! Sensor TCS34725 Terdeteksi.");
  } else {
    Serial.println("ERROR: Sensor tidak terdeteksi!");
    Serial.println("Cek lagi kabel SDA (P21) dan SCL (P22) ya.");
    while (1); 
  }
}

void loop() {
  uint16_t r, g, b, c, colorTemp, lux;

  // 3. AMBIL SEMUA DATA MENTAH (RAW DATA)
  tcs.getRawData(&r, &g, &b, &c);

  // 4. HITUNG LUX & SUHU WARNA (Pakai fungsi versi terbaru!)
  colorTemp = tcs.calculateColorTemperature(r, g, b);
  lux = tcs.calculateLux(r, g, b);

  // 5. TAMPILKAN SEMUA HASILNYA KE SERIAL MONITOR
  Serial.print("Cahaya Mentah (Clear): "); Serial.print(c);
  Serial.print(" | Merah (R): "); Serial.print(r);
  Serial.print(" | Hijau (G): "); Serial.print(g);
  Serial.print(" | Biru (B): "); Serial.print(b);
  Serial.print(" || Intensitas (Lux): "); Serial.print(lux);
  Serial.print(" | Suhu Warna: "); Serial.print(colorTemp); Serial.println(" K");

  delay(1000); 
}