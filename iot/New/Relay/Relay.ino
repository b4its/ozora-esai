// ==========================================
// KODE TES HARDWARE OZORA V2 (SHUTDOWN LINEAR)
// I2C LCD + TCS34725 + 4 RELAY + CUSTOM DIMMER
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
volatile int currentDimmerPower = 100; // AWAL DIMULAI DARI 100% (NYALA FULL)
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
  
  Serial.println("\n\n=== MEMULAI SISTEM OZORA V2 (SHUTDOWN TEST) ===");

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
  // KONDISI AWAL (NYALA TOTAL) -> Relay Active High (HIGH = Nyala)
  // =======================================================
  digitalWrite(RELAY_1_PIN, HIGH);
  digitalWrite(RELAY_2_PIN, HIGH);
  digitalWrite(RELAY_3_PIN, HIGH);
  digitalWrite(RELAY_4_PIN, HIGH);
  digitalWrite(LED_HIJAU_PIN, HIGH); 
  digitalWrite(TCS_LED_PIN, HIGH);  
  currentDimmerPower = 100; // Dimmer langsung gas 100%
  // =======================================================

  // --- 2. Inisialisasi Dimmer & Timer ---
  dimmerTimer = timerBegin(1000000); 
  timerAttachInterrupt(dimmerTimer, &onDimmerTimer); 
  attachInterrupt(digitalPinToInterrupt(DIMMER_ZC_PIN), zcDetectISR, RISING); 

  // --- 3. Inisialisasi LCD ---
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM NYALA!");
  delay(1000);

  // --- 4. Cek Sensor TCS34725 ---
  if (tcs.begin()) {
    Serial.println("[OK] Sensor TCS Ditemukan!");
  } else {
    Serial.println("[ERROR] TCS Tidak Ditemukan!");
    lcd.clear();
    lcd.print("TCS ERROR!");
    while (1); 
  }
  delay(1500);

  // --- 5. SHUTDOWN SEQUENCE MESIN (DARI NYALA KE MATI URUT MAJU) ---
  Serial.println("\n[SYSTEM] Memulai Sekuensi Mematikan Mesin...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SHUTDOWN MESIN:");

  int jedaAman = 800; // Jeda 0.8 detik per mesin

  // TAHAP 1: Ozonator (Dimmer) Turun Perlahan ke 0%
  lcd.setCursor(0, 1); lcd.print("Ozonator OFF... ");
  Serial.println("[PROSES] Menurunkan daya Dimmer (Ozonator) ke 0%...");
  for (int p = 100; p >= 0; p -= 2) {
    currentDimmerPower = p;
    delay(20);
  }
  lcd.setCursor(0, 1); lcd.print("Dimmer 0%       ");
  Serial.println("[NONAKTIF] AC Dimmer Ozonator -> MATI TOTAL (0%)");
  delay(jedaAman);

  // TAHAP 2: Relay 1 (Pompa) Mati
  digitalWrite(RELAY_1_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Pompa OFF       ");
  Serial.println("[NONAKTIF] Relay 1 (Pompa) -> OFF");
  delay(jedaAman);

  // TAHAP 3: Relay 2 (Chiller) Mati
  digitalWrite(RELAY_2_PIN, LOW);

  lcd.setCursor(0, 1); lcd.print("Chiller OFF     ");
  Serial.println("[NONAKTIF] Relay 2 (Chiller) -> OFF");
  delay(jedaAman);

  // TAHAP 4: Relay 3 Mati
  digitalWrite(RELAY_3_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Relay 3 OFF     ");
  Serial.println("[NONAKTIF] Relay 3 -> OFF");
  delay(jedaAman);

  // TAHAP 5: Relay 4 Mati
  digitalWrite(RELAY_4_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Relay 4 OFF     ");
  Serial.println("[NONAKTIF] Relay 4 -> OFF");
  delay(jedaAman);

  // TAHAP 6: Lampu Hijau Mati (Tanda sistem sudah nonaktif total)
  digitalWrite(LED_HIJAU_PIN, LOW);
  lcd.setCursor(0, 1); lcd.print("Lampu Hijau OFF ");
  Serial.println("[NONAKTIF] Lampu Hijau -> OFF");
  
  delay(1000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM OFF!     ");
  delay(1000);
  lcd.clear();
}

void loop() {
  // === FASE PENGUMPULAN DATA SENSOR (MESIN MATI) ===
  // Alat hanya fokus mengambil data warna air dengan layar penuh data
  
  uint16_t r, g, b, c; 
  uint16_t colorTemp, lux;

  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux = tcs.calculateLux(r, g, b);

  // TAMPILAN DASHBOARD LCD KHUSUS SENSOR
  lcd.setCursor(0, 0);
  lcd.print("R:"); lcd.print(r);
  lcd.print(" G:"); lcd.print(g);
  lcd.print(" B:"); lcd.print(b);
  lcd.print("    "); 

  lcd.setCursor(0, 1);
  lcd.print("C:"); lcd.print(c);
  lcd.print(" Lx:"); lcd.print(lux);
  lcd.print(" K:"); lcd.print(colorTemp);
  lcd.print("    "); 

  // Kirim Data ke Serial Monitor
  Serial.println("====================================");
  Serial.println("STATUS: MESIN OFF | SENSOR STANDBY");
  Serial.print("R: "); Serial.print(r);
  Serial.print(" | G: "); Serial.print(g);
  Serial.print(" | B: "); Serial.println(b);
  Serial.print("Clear: "); Serial.print(c);
  Serial.print(" | Lux: "); Serial.print(lux);
  Serial.print(" | Kelvin: "); Serial.println(colorTemp);
  Serial.println("====================================\n");

  delay(1000); 
}