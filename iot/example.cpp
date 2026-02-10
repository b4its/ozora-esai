#include <Wire.h>
#include "Adafruit_TCS34725.h"

/* KONFIGURASI SENSOR
 * Pakai Gain 4X dan Time 50ms biar responsif buat air
 */
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

void setup() {
  Serial.begin(115200);
  
  // Cek Koneksi Sensor
  if (tcs.begin()) {
    Serial.println("Sensor OK! Membaca Data Mentah...");
    Serial.println("R\tG\tB\tC\tTemp\tLux"); // Header Kolom
  } else {
    Serial.println("ERROR: Sensor Tidak Ditemukan!");
    Serial.println("Cek kabel SDA (21) dan SCL (22)");
    while (1); // Stop
  }
}

void loop() {
  uint16_t r, g, b, c, colorTemp, lux;

  // 1. Ambil Data Mentah (Raw Data)
  tcs.getRawData(&r, &g, &b, &c);

  // 2. Hitung Temperatur Warna & Intensitas Cahaya (Fitur Library)
  // Ini berguna buat analisis tambahan di laporanmu
  colorTemp = tcs.calculateColorTemperature(r, g, b);
  lux = tcs.calculateLux(r, g, b);

  // 3. Tampilkan SEMUA DATA (Format Tabular biar rapi)
  Serial.print(r); Serial.print("\t");    // Red
  Serial.print(g); Serial.print("\t");    // Green
  Serial.print(b); Serial.print("\t");    // Blue
  Serial.print(c); Serial.print("\t");    // Clear (Penting buat kekeruhan)
  Serial.print(colorTemp); Serial.print("\t"); // Suhu Warna (Kelvin)
  Serial.print(lux); Serial.println("");       // Lux (Kecerahan)

  delay(500); // Update data tiap setengah detik (biar gak pusing liatnya)
}