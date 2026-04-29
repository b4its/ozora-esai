// ==========================================
// KODE TES HARDWARE OZORA V2 (MODE KALIBRASI SENSOR)
// SEMUA MESIN MATI PERMANEN - FOKUS BACA DATA SENSOR
// ==========================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "Adafruit_TCS34725.h"

// --- Definisi Pin (Zona Hijau) ---
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
// --- CUSTOM DRIVER AC DIMMER ESP32 V3 ---
// ==========================================
volatile int currentDimmerPower = 0; // KUNCI DI 0% (MATI TOTAL) PERMANEN
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== MEMULAI SISTEM OZORA V2 (MODE KALIBRASI SENSOR) ===");

  // --- 1. Set Mode Pin ---
  pinMode(TCS_LED_PIN, OUTPUT);
  pinMode(LED_HIJAU_PIN, OUTPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(RELAY_4_PIN, OUTPUT);
  pinMode(DIMMER_DIM_PIN, OUTPUT);
  pinMode(DIMMER_ZC_PIN, INPUT_PULLUP);

  // =======================================================
  // KUNCI MATI SEMUA AKTUATOR DARI AWAL
  // Relay Active High (LOW = Mati)
  // =======================================================
  digitalWrite(RELAY_1_PIN, LOW);
  digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(RELAY_3_PIN, LOW);
  digitalWrite(RELAY_4_PIN, LOW);
  digitalWrite(LED_HIJAU_PIN, LOW); // Lampu Hijau Mati
  digitalWrite(DIMMER_DIM_PIN, LOW); 
  
  // Hanya LED putih bawaan sensor yang dinyalakan agar bisa baca warna air
  digitalWrite(TCS_LED_PIN, HIGH);  
  currentDimmerPower = 0; // Pastikan algoritma dimmer nol
  // =======================================================

  // --- 2. Inisialisasi Dimmer & Timer ---
  dimmerTimer = timerBegin(1000000); 
  timerAttachInterrupt(dimmerTimer, &onDimmerTimer); 
  attachInterrupt(digitalPinToInterrupt(DIMMER_ZC_PIN), zcDetectISR, RISING); 

  // --- 3. Inisialisasi LCD ---
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("BOOTING SENSOR..");
  delay(1000);

  // --- 4. Cek Sensor TCS34725 ---
  if (tcs.begin()) {
    Serial.println("[OK] Sensor TCS Ditemukan dan Siap!");
    lcd.clear();
    lcd.print("SENSOR READY!");
    delay(1000);
  } else {
    Serial.println("[ERROR] TCS Tidak Ditemukan!");
    lcd.clear();
    lcd.print("TCS ERROR!");
    while (1); // Berhenti di sini kalau sensor putus
  }
}

void loop() {
  // === FASE PENGUMPULAN DATA SENSOR NON-STOP ===
  
  uint16_t r, g, b, c; 
  uint16_t colorTemp, lux;

  // Mengambil data presisi dari sensor
  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux = tcs.calculateLux(r, g, b);

  // TAMPILAN DASHBOARD LCD (Fokus Penuh ke Data)
  lcd.setCursor(0, 0);
  lcd.print("R:"); lcd.print(r);
  lcd.print(" G:"); lcd.print(g);
  lcd.print(" B:"); lcd.print(b);
  lcd.print("    "); // Spasi kosong untuk membersihkan sisa teks

  lcd.setCursor(0, 1);
  lcd.print("C:"); lcd.print(c);
  lcd.print(" Lx:"); lcd.print(lux);
  lcd.print(" K:"); lcd.print(colorTemp);
  lcd.print("    ");

  // Kirim Data ke Serial Monitor untuk keperluan Dataset ML
  Serial.println("====================================");
  Serial.println("[MODE KALIBRASI] - MESIN TOTAL OFF");
  Serial.print("R: "); Serial.print(r);
  Serial.print(" | G: "); Serial.print(g);
  Serial.print(" | B: "); Serial.println(b);
  Serial.print("Clear: "); Serial.print(c);
  Serial.print(" | Lux: "); Serial.print(lux);
  Serial.print(" | Kelvin: "); Serial.println(colorTemp);
  Serial.println("====================================\n");

  delay(1000); // Sampling data stabil setiap 1 detik
}