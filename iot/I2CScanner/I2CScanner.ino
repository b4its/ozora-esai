/**
 * ============================================================
 *  ESP32 I2C Scanner — TCS34725 & LCD Display
 *  Version : 2.1
 *  Features:
 *    - Scan seluruh alamat I2C (1-127)
 *    - Deteksi TCS34725 Color Sensor (0x29)
 *    - Kontrol LED TCS34725 via GPIO 4
 *    - Tampilkan hasil scan di Serial Monitor
 *    - Tampilkan hasil scan di I2C LCD 16x2
 *    - Auto-detect LCD I2C (0x27 / 0x3F)
 *    - Loop scan setiap 5 detik
 * ============================================================
 *
 *  Koneksi TCS34725 ke ESP32:
 *    TCS34725 VIN  → ESP32 3.3V
 *    TCS34725 GND  → ESP32 GND
 *    TCS34725 SCL  → ESP32 GPIO 22
 *    TCS34725 SDA  → ESP32 GPIO 21
 *    TCS34725 LED  → ESP32 GPIO 4  (LED iluminasi)
 *
 *  Library yang dibutuhkan:
 *    1. Adafruit TCS34725
 *    2. LiquidCrystal I2C (by Frank de Brabander)
 *    3. Wire (built-in)
 * ============================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "Adafruit_TCS34725.h"

// Pin I2C — ESP32 default
#define I2C_SDA  21
#define I2C_SCL  22
#define TCS_LED  4       // GPIO 4 untuk LED iluminasi TCS34725

#define ADDR_TCS34725  0x29

// LCD — akan diinisialisasi setelah auto-detect
LiquidCrystal_I2C* lcd = NULL;
uint8_t lcdAddr = 0;

int lastDeviceCount = -1;
bool lastTCSStatus  = false;
unsigned long lastLcdUpdate = 0;

Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);

// ============================================================
//  FUNGSI: Deteksi LCD I2C
// ============================================================
bool detectLCD() {
  uint8_t candidates[] = {0x27, 0x3F};
  for (int i = 0; i < 2; i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) {
      lcdAddr = candidates[i];
      Serial.printf("[LCD] Terdeteksi di 0x%02X\n", lcdAddr);
      return true;
    }
  }
  return false;
}

// ============================================================
//  FUNGSI: Update LCD display
// ============================================================
void updateLCD(int count, bool tcsOK, uint16_t r, uint16_t g, uint16_t b) {
  if (!lcd) return;

  unsigned long now = millis();
  if (now - lastLcdUpdate < 2000) return;
  lastLcdUpdate = now;

  // Baris 1: jumlah device + TCS status
  lcd->setCursor(0, 0);
  lcd->printf("I2C:%d ", count);
  if (tcsOK) {
    lcd->print("TCS:OK");
  } else {
    lcd->print("TCS:--");
  }

  // Baris 2: nilai RGB
  lcd->setCursor(0, 1);
  lcd->printf("R:%03u G:%03u B:%03u", r, g, b);
}

// ============================================================
//  FUNGSI: Tampilkan pesan di LCD
// ============================================================
void lcdMessage(const char* line1, const char* line2) {
  if (!lcd) return;
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print(line1);
  lcd->setCursor(0, 1);
  lcd->print(line2);
}

// ============================================================
//  FUNGSI: Baca register TCS34725
// ============================================================
bool checkTCS34725Register() {
  Wire.beginTransmission(ADDR_TCS34725);
  Wire.write(0x12);
  Wire.endTransmission();
  Wire.requestFrom(ADDR_TCS34725, (uint8_t)1);
  if (Wire.available()) {
    uint8_t id = Wire.read();
    Serial.printf("  → ID Register (0x12): 0x%02X", id);
    if (id == 0x44 || id == 0x4D) {
      Serial.print(" ✅ TCS34725");
    } else {
      Serial.print(" ⚠ ID unknown");
    }
    Serial.println();
    return (id == 0x44 || id == 0x4D);
  }
  Serial.println("  → Gagal baca ID register");
  return false;
}

// ============================================================
//  FUNGSI: Inisialisasi Adafruit
// ============================================================
bool tryAdafruitInit() {
  if (tcs.begin()) {
    Serial.println("  → Adafruit TCS34725.begin() ✅");
    return true;
  }
  Serial.println("  → Adafruit TCS34725.begin() ❌");
  return false;
}

// ============================================================
//  FUNGSI: Baca data sensor
// ============================================================
bool readTCS34725(uint16_t &r, uint16_t &g, uint16_t &b, uint16_t &c, uint16_t &ct, uint16_t &lux) {
  digitalWrite(TCS_LED, HIGH);
  delay(50); // stabilisasi cahaya

  tcs.getRawData(&r, &g, &b, &c);
  ct  = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux = tcs.calculateLux(r, g, b);

  digitalWrite(TCS_LED, LOW);

  Serial.println("  ── Data Sensor ──");
  Serial.printf("  Clear:%5u  R:%5u  G:%5u  B:%5u\n", c, r, g, b);
  Serial.printf("  Temp:%uK  Lux:%u\n", ct, lux);

  if (r > g && r > b) Serial.println("  Dominan: MERAH");
  else if (g > r && g > b) Serial.println("  Dominan: HIJAU");
  else if (b > r && b > g) Serial.println("  Dominan: BIRU");
  else Serial.println("  Warna: CAMPURAN");
  Serial.println("  ─────────────────");
  return true;
}

// ============================================================
//  FUNGSI: Nama device I2C
// ============================================================
const char* i2cDeviceName(uint8_t addr) {
  switch (addr) {
    case 0x29: return "TCS34725";
    case 0x27: case 0x3F: return "LCD1602";
    case 0x76: return "BMP280";
    case 0x77: return "BME280";
    case 0x68: return "MPU6050";
    case 0x3C: case 0x3D: return "SSD1306";
    case 0x48: return "ADS1115";
    case 0x50: return "AT24C32";
    case 0x57: return "AT24C256";
    case 0x5C: return "AM2320";
    case 0x40: return "PCA9685";
    case 0x70: return "HT16K33";
    default: return NULL;
  }
}

// ============================================================
//  FUNGSI: Scan I2C
// ============================================================
int scanI2C() {
  Serial.println("\n🔍 Scanning I2C bus...");
  Serial.printf("   SDA:GPIO%d | SCL:GPIO%d | Clock:%uHz\n", I2C_SDA, I2C_SCL, Wire.getClock());

  int count = 0;
  uint8_t foundDevices[32];
  uint8_t foundIdx = 0;

  for (uint8_t addr = 1; addr < 127 && foundIdx < 32; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      foundDevices[foundIdx++] = addr;
      count++;
      const char* name = i2cDeviceName(addr);
      Serial.printf("  [0x%02X]", addr);
      if (name) Serial.printf(" → %s", name);
      Serial.println();

      if (addr == ADDR_TCS34725) {
        checkTCS34725Register();
      }
    }
  }

  Serial.println();
  if (count == 0) {
    Serial.println("  ❌ Tidak ada perangkat I2C!");
    Serial.println("  ￫ Cek kabel SDA/SCL/GND/VCC");
    Serial.println("  ￫ Pastikan tegangan 3.3V");
    Serial.println("  ￫ Coba pull-up 4.7kΩ ke 3.3V");
    if (lcd) lcdMessage("No I2C device!", "Check wiring");
  } else {
    Serial.printf("  ✅ %d perangkat I2C ditemukan\n", count);
  }

  return count;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n╔══════════════════════════════════════╗"));
  Serial.println(F("║   ESP32 I2C Scanner v2.0 + LCD      ║"));
  Serial.println(F("╚══════════════════════════════════════╝\n"));

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.printf("I2C: SDA=%d, SCL=%d, Clock=%uHz\n", I2C_SDA, I2C_SCL, Wire.getClock());

  // LED iluminasi TCS34725
  pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_LED, LOW);
  Serial.printf("[LED] GPIO%d initialized (OFF)\n", TCS_LED);

  // Deteksi LCD
  if (detectLCD()) {
    lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcdMessage("ESP32 I2C Scan", "v2.0 starting...");
    Serial.printf("[LCD] ✅ 0x%02X (16x2)\n", lcdAddr);
  } else {
    Serial.println("[LCD] ❌ Tidak terdeteksi");
  }

  delay(1500);

  // Scan pertama
  int found = scanI2C();
  if (lcd) {
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->printf("%d I2C device(s)", found);
  }

  uint16_t r = 0, g = 0, b = 0, c = 0, ct = 0, lux = 0;
  bool tcsOK = false;

  if (found > 0) {
    Wire.beginTransmission(ADDR_TCS34725);
    if (Wire.endTransmission() == 0) {
      Serial.println("\n📡 Inisialisasi TCS34725...");
      if (tryAdafruitInit()) {
        tcsOK = true;
        readTCS34725(r, g, b, c, ct, lux);
      }
    }
  }

  if (lcd) {
    lcd->clear();
    if (tcsOK) {
      lcd->setCursor(0, 0);
      lcd->printf("TCS R:%03u G:%03u", r, g);
      lcd->setCursor(0, 1);
      lcd->printf("B:%03u L:%04u", b, lux);
    } else {
      lcd->setCursor(0, 0);
      lcd->printf("I2C Scan: %d dev", found);
      lcd->setCursor(0, 1);
      lcd->printf("No TCS34725");
    }
  }

  lastDeviceCount = found;
  lastTCSStatus = tcsOK;
  lastLcdUpdate = millis();

  Serial.println("\n────────────────────────────");
  Serial.println(" Loop scan setiap 5 detik...");
  Serial.println("────────────────────────────\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  int found = scanI2C();
  uint16_t r = 0, g = 0, b = 0, c = 0, ct = 0, lux = 0;
  bool tcsOK = false;

  if (found > 0) {
    Wire.beginTransmission(ADDR_TCS34725);
    if (Wire.endTransmission() == 0) {
      tcsOK = tryAdafruitInit();
      if (tcsOK) readTCS34725(r, g, b, c, ct, lux);
    }
  }

  // Update LCD jika ada perubahan
  if (lcd && (found != lastDeviceCount || tcsOK != lastTCSStatus)) {
    lcd->clear();
    if (tcsOK) {
      lcd->setCursor(0, 0);
      lcd->printf("TCS R:%03u G:%03u", r, g);
      lcd->setCursor(0, 1);
      lcd->printf("B:%03u L:%04u", b, lux);
    } else {
      lcd->setCursor(0, 0);
      lcd->printf("I2C: %d device(s)", found);
      lcd->setCursor(0, 1);
      lcd->printf("No TCS34725");
    }
    lastDeviceCount = found;
    lastTCSStatus = tcsOK;
    lastLcdUpdate = millis();
  }

  // Periodic LCD update with sensor data
  if (lcd && tcsOK && found == lastDeviceCount && tcsOK == lastTCSStatus) {
    updateLCD(found, tcsOK, r, g, b);
  }

  Serial.println("\n⏳ Scan ulang dalam 5 detik...");
  Serial.println("────────────────────────────\n");
  delay(5000);
}