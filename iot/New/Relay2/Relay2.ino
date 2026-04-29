// ==========================================
// KODE TES HARDWARE OZORA V2 (LCD KHUSUS SENSOR)
// I2C LCD (Full Sensor) + 4 RELAY + CUSTOM DIMMER
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
volatile int currentDimmerPower = 0; // 0 - 100%
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
  
  Serial.println("\n\n=== MEMULAI SISTEM OZORA V2 ===");

  // --- 1. Set Mode Pin ---
  pinMode(TCS_LED_PIN, OUTPUT);
  pinMode(LED_HIJAU_PIN, OUTPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(RELAY_4_PIN, OUTPUT);
  pinMode(DIMMER_DIM_PIN, OUTPUT);
  pinMode(DIMMER_ZC_PIN, INPUT_PULLUP);

  // KONDISI AWAL (MATI TOTAL) -> Relay Active High (LOW = Mati)
  digitalWrite(RELAY_1_PIN, LOW);
  digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(RELAY_3_PIN, LOW);
  digitalWrite(RELAY_4_PIN, LOW);
  digitalWrite(LED_HIJAU_PIN, LOW); 
  digitalWrite(TCS_LED_PIN, HIGH);  
  digitalWrite(DIMMER_DIM_PIN, LOW); 

  // --- 2. Inisialisasi Dimmer & Timer ---
  dimmerTimer = timerBegin(1000000); 
  timerAttachInterrupt(dimmerTimer, &onDimmerTimer); 
  attachInterrupt(digitalPinToInterrupt(DIMMER_ZC_PIN), zcDetectISR, RISING); 
  currentDimmerPower = 0; 

  // --- 3. Inisialisasi LCD ---
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("BOOTING OZORA V2");
  delay(1000);

  // --- 4. Cek Sensor TCS34725 ---
  if (tcs.begin()) {
    Serial.println("[OK] Sensor TCS Ditemukan!");
    lcd.setCursor(0, 1);
    lcd.print("TCS34725: OK    ");
  } else {
    Serial.println("[ERROR] TCS Tidak Ditemukan!");
    lcd.setCursor(0, 1);
    lcd.print("TCS34725: ERROR ");
    while (1); 
  }
  delay(1500);

  // --- 5. STARTUP SEQUENCE MESIN ---
  Serial.println("\n[SYSTEM] Memulai Sekuensi Mesin Utama...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("STARTUP MESIN:");

  int jedaAman = 800; 

  // 1. Ozonator (Dimmer Naik)
  lcd.setCursor(0, 1); lcd.print("Ozonator ON...  ");
  for (int p = 0; p <= 100; p += 2) {
    currentDimmerPower = p;
    delay(20);
  }

  // 2. Relay 1 (Pompa)
  delay(jedaAman);
  digitalWrite(RELAY_1_PIN, HIGH);
  lcd.setCursor(0, 1); lcd.print("Pompa ON        ");

  // 3. Relay 2 (Chiller)
  delay(jedaAman);
  digitalWrite(RELAY_2_PIN, HIGH);
  lcd.setCursor(0, 1); lcd.print("Chiller ON      ");

  // 4. Relay 3 
  delay(jedaAman);
  digitalWrite(RELAY_3_PIN, HIGH);
  lcd.setCursor(0, 1); lcd.print("Relay 3 ON      ");

  // 5. Relay 4 
  delay(jedaAman);
  digitalWrite(RELAY_4_PIN, HIGH);
  lcd.setCursor(0, 1); lcd.print("Relay 4 ON      ");

  // 6. Lampu Hijau (Ready)
  delay(jedaAman);
  digitalWrite(LED_HIJAU_PIN, HIGH);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM READY!   ");
  delay(1000);
  lcd.clear();
}

void loop() {
  // === FASE PENGUMPULAN DATA SENSOR ===
  
  uint16_t r, g, b, c; 
  uint16_t colorTemp, lux;

  // 1. Ambil Data dari Sensor
  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux = tcs.calculateLux(r, g, b);

  // 2. TAMPILAN DASHBOARD LCD KHUSUS SENSOR
  // Baris Atas: Red, Green, Blue
  lcd.setCursor(0, 0);
  lcd.print("R:"); lcd.print(r);
  lcd.print(" G:"); lcd.print(g);
  lcd.print(" B:"); lcd.print(b);
  lcd.print("    "); // Spasi untuk menimpa teks lama

  // Baris Bawah: Clear, Lux, Kelvin (Suhu Warna)
  lcd.setCursor(0, 1);
  lcd.print("C:"); lcd.print(c);
  lcd.print(" Lx:"); lcd.print(lux);
  lcd.print(" K:"); lcd.print(colorTemp);
  lcd.print("    "); 

  // 3. Kirim Data ke Serial Monitor (Untuk Database ML)
  Serial.println("====================================");
  Serial.print("R: "); Serial.print(r);
  Serial.print(" | G: "); Serial.print(g);
  Serial.print(" | B: "); Serial.println(b);
  Serial.print("Clear: "); Serial.print(c);
  Serial.print(" | Lux: "); Serial.print(lux);
  Serial.print(" | Kelvin: "); Serial.println(colorTemp);
  Serial.println("====================================\n");

  delay(1000); 
}