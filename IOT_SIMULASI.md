# Simulasi Perangkat IoT — ESP32 + TCS34725 (CSensortitu v5.3)

Dokumen ini berisi panduan simulasi alur kerja perangkat IoT `CSensortitu.ino` dan daftar perintah **cURL** untuk menguji setiap endpoint API tanpa perangkat fisik.

---

## Alur Kerja Perangkat

```
Boot ESP32
  │
  ├─ Reset WiFi credentials → Buka Portal "ESP32_Ozora_Portal"
  │    User input: SSID WiFi + Password + API Token
  │
  ├─ Connect WiFi → mDNS aktif (http://esp32sensor.local)
  │
  ├─ Heartbeat (tiap 15 detik) → POST /api/device/heartbeat/
  │    Response: { target_status, active_experiment }
  │    ├─ target_status = true  → Jalankan startup bertahap (5 dtk/alat)
  │    └─ target_status = false → Jalankan shutdown bertahap
  │
  ├─ MACHINE_STARTING (jika target_status=true)
  │    Step 1: Dimmer/Ozonator ON
  │    Step 2: Relay 1 ON
  │    Step 3: Relay 2 ON
  │    Step 4: Relay 3 ON + Sensor TCS ON
  │
  ├─ MACHINE_ON → Kirim data sensor tiap 10 detik
  │    ├─ POST /api/receive-data/   (data RGB, temp, lux)
  │    └─ POST /api/device/sterile-check/  (cek kualitas air)
  │
  └─ MACHINE_STOPPING (jika target_status=false)
       Step 1: LCD notif "Air Jernih"
       Step 2: Sensor OFF
       Step 3: Relay 1 OFF → Relay 2 OFF → Relay 3 OFF → Dimmer OFF
```

---

## Prasyarat

```bash
# API Token — dapatkan dari dashboard web setelah login
TOKEN="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

# Base URL — ganti dengan domain/server Django
BASE="https://ozora.b4its.cloud"
# atau untuk local development:
# BASE="http://127.0.0.1:8000"
```

---

## Endpoint API & cURL Testing

### 1. Heartbeat (Auto-register Device)

Django endpoint: `POST /api/device/heartbeat/`

ESP32 mengirim tiap 15 detik. Server membalas dengan `target_status` (ON/OFF) dan `active_experiment`.

```bash
curl -X POST "${BASE}/api/device/heartbeat/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "C8:F0:9E:AA:BB:CC",
    "device_name": "ESP32_Ozora_Portal",
    "ip_local": "192.168.1.100",
    "ssid": "MyWiFi",
    "rssi": -65,
    "firmware": "5.3"
  }'
```

**Response sukses (200):**
```json
{
  "status": "ok",
  "device_id": "C8:F0:9E:AA:BB:CC",
  "target_status": false,
  "is_sterile": false,
  "active_experiment": 1,
  "registered": false,
  "message": "Heartbeat received"
}
```

>> **Simulasi:** Setelah heartbeat pertama sukses, server mencatat device = *online* selama 30 detik.

---

### 2. Kontrol ON/OFF dari Website

Website mengirim perintah ke server, ESP32 membaca `target_status` di response heartbeat berikutnya.

**Nyalakan proses (ON):**

```bash
curl -X POST "${BASE}/api/device/control/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "C8:F0:9E:AA:BB:CC",
    "target_status": true
  }'
```

**Matikan proses (OFF):**

```bash
curl -X POST "${BASE}/api/device/control/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "C8:F0:9E:AA:BB:CC",
    "target_status": false
  }'
```

**Response:**
```json
{
  "status": "success",
  "message": "Device C8:F0:9E:AA:BB:CC diset ke ON",
  "target_status": true
}
```

>> **Simulasi:** Jalankan `curl` ON, lalu Heartbeat (langkah 1) — response heartbeat akan menampilkan `target_status: true`.

---

### 3. Kirim Data Sensor

Django endpoint: `POST /api/receive-data/`

ESP32 mengirim tiap 10 detik setelah mesin ON penuh.

```bash
curl -X POST "${BASE}/api/receive-data/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "C8:F0:9E:AA:BB:CC",
    "raw_light": 42000,
    "red": 28500,
    "green": 14200,
    "blue": 8300,
    "temp": 4500,
    "lux": 320,
    "experiment": 1
  }'
```

**Response sukses (201):**
```json
{
  "status": "success",
  "message": "Data diterima untuk {username}",
  "experiment_id": 1
}
```

**Variasikan nilai sensor untuk simulasi kondisi berbeda:**

| Kondisi Air | raw_light | red  | green | blue | temp | lux |
|-------------|-----------|------|-------|------|------|-----|
| **Jernih** | 55000 | 32000 | 31000 | 30000 | 5000 | 450 |
| **Sedang (kuning)** | 42000 | 28500 | 14200 | 8300 | 4500 | 320 |
| **Keruh (merah)** | 18000 | 35000 | 8500 | 4200 | 3800 | 150 |
| **Hijau pekat** | 22000 | 8800 | 29000 | 7500 | 4200 | 200 |
| **Biru tua** | 15000 | 6500 | 7200 | 31000 | 4800 | 120 |
| **Sangat keruh** | 5000 | 3000 | 2500 | 2000 | 3500 | 40 |

**Contoh: Simulasi air jernih**
```bash
curl -X POST "${BASE}/api/receive-data/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "C8:F0:9E:AA:BB:CC",
    "raw_light": 55000,
    "red": 32000,
    "green": 31000,
    "blue": 30000,
    "temp": 5000,
    "lux": 450,
    "experiment": 1
  }'
```

---

### 4. Cek Steril (Kualitas Air)

Django endpoint: `POST /api/device/sterile-check/`

Dipanggil SETIAP setelah kirim data sensor. Server memeriksa threshold dan memutuskan apakah air sudah jernih.

```bash
curl -X POST "${BASE}/api/device/sterile-check/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "C8:F0:9E:AA:BB:CC",
    "raw_light": 55000,
    "red": 32000,
    "green": 31000,
    "blue": 30000,
    "lux": 450
  }'
```

**Response (belum jernih):**
```json
{
  "status": "ok",
  "device_id": "C8:F0:9E:AA:BB:CC",
  "target_status": true,
  "is_sterile": false,
  "just_triggered": false,
  "confirm_count": 1,
  "confirm_needed": 3,
  "message": "Belum jernih (1/3)"
}
```

**Response (jernih — mesin dimatikan otomatis):**
```json
{
  "status": "ok",
  "device_id": "C8:F0:9E:AA:BB:CC",
  "target_status": false,
  "is_sterile": true,
  "just_triggered": true,
  "confirm_count": 3,
  "confirm_needed": 3,
  "message": "Air sudah jernih! Mesin dimatikan."
}
```

> **Catatan:** Kirim data jernih berturut-turut (sesuai `sterile_confirm_count`, default 3x) hingga server memutuskan `is_sterile: true` dan mesin mati otomatis.

---

### 5. Cek Device Online

```bash
curl -X GET "${BASE}/api/devices/online/" \
  -H "Authorization: Token ${TOKEN}"
```

```bash
curl -X GET "${BASE}/api/devices/probe/" \
  -H "Authorization: Token ${TOKEN}"
```

---

### 6. Experiment Room CRUD

**Buat experiment baru:**
```bash
curl -X POST "${BASE}/api/experiments/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Batch Treatment #1",
    "suhu": 30,
    "flow_speed": 2.5,
    "ph_target": 7.0
  }'
```

**Lihat semua experiment:**
```bash
curl -X GET "${BASE}/api/experiments/" \
  -H "Authorization: Token ${TOKEN}"
```

**Detail experiment:**
```bash
curl -X GET "${BASE}/api/experiments/1/" \
  -H "Authorization: Token ${TOKEN}"
```

**Edit experiment:**
```bash
curl -X PUT "${BASE}/api/experiments/1/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Batch Treatment #1",
    "suhu": 32,
    "flow_speed": 3.0,
    "ph_target": 7.2
  }'
```

**Data sensor milik experiment:**
```bash
# Experiment ID 1
curl -X GET "${BASE}/api/experiments/1/data/" \
  -H "Authorization: Token ${TOKEN}"

# Data tanpa experiment (default)
curl -X GET "${BASE}/api/experiments/0/data/" \
  -H "Authorization: Token ${TOKEN}"
```

---

### 7. Set Active Experiment untuk Device

Mengaitkan device ke experiment room. Data sensor yang masuk otomatis tertandai.

```bash
curl -X POST "${BASE}/api/device/active-experiment/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "C8:F0:9E:AA:BB:CC",
    "experiment_id": 1
  }'
```

---

## Skenario Simulasi Lengkap

### Skenario 1: Aliran Normal (ON → Kirim Data → OFF Manual)

```bash
# 1. Register device via heartbeat
#    (langkah ini cukup sekali, device otomatis online 30 detik)
curl -X POST "${BASE}/api/device/heartbeat/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"C8:F0:9E:AA:BB:CC","device_name":"ESP32_Ozora_Portal","ip_local":"192.168.1.100","ssid":"MyWiFi","rssi":-65,"firmware":"5.3"}'

# 2. Nyalakan mesin dari website
curl -X POST "${BASE}/api/device/control/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"C8:F0:9E:AA:BB:CC","target_status":true}'

# 3. Simulasi kirim data sensor (mesin menyala)
curl -X POST "${BASE}/api/receive-data/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"C8:F0:9E:AA:BB:CC","raw_light":42000,"red":28500,"green":14200,"blue":8300,"temp":4500,"lux":320,"experiment":1}'

# 4. Cek kualitas air
curl -X POST "${BASE}/api/device/sterile-check/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"C8:F0:9E:AA:BB:CC","raw_light":42000,"red":28500,"green":14200,"blue":8300,"lux":320}'

# 5. Matikan mesin dari website
curl -X POST "${BASE}/api/device/control/" \
  -H "Authorization: Token ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"C8:F0:9E:AA:BB:CC","target_status":false}'
```

### Skenario 2: Auto-Stop karena Air Jernih

Kirim data jernih berturut-turut (minimal 3x) hingga server memutuskan mesin mati:

```bash
# Loop simulasi air jernih (ulangi 3-4 kali)
for i in 1 2 3 4; do
  echo "=== Iterasi $i ==="
  curl -s -X POST "${BASE}/api/receive-data/" \
    -H "Authorization: Token ${TOKEN}" \
    -H "Content-Type: application/json" \
    -d '{"device_id":"C8:F0:9E:AA:BB:CC","raw_light":55000,"red":32000,"green":31000,"blue":30000,"temp":5000,"lux":450,"experiment":1}'
  echo ""
  curl -s -X POST "${BASE}/api/device/sterile-check/" \
    -H "Authorization: Token ${TOKEN}" \
    -H "Content-Type: application/json" \
    -d '{"device_id":"C8:F0:9E:AA:BB:CC","raw_light":55000,"red":32000,"green":31000,"blue":30000,"lux":450}'
  echo ""
done
```

Perhatikan di response terakhir: `"is_sterile": true, "target_status": false`

---

## Catatan Penting

| Konstanta Firmware | Nilai | Keterangan |
|-------------------|-------|------------|
| `SEND_INTERVAL` | 10000 ms | Interval kirim data sensor |
| `HEARTBEAT_INTERVAL` | 15000 ms | Interval heartbeat |
| `STEP_INTERVAL` | 5000 ms | Jeda antar aktuator startup/shutdown |
| `MAX_RECONNECT_ATTEMPTS` | 3 | Maks gagal reconnect sebelum buka portal |
| AP SSID | `ESP32_Ozora_Portal` | Nama WiFi portal |
| AP Password | `Admin1234` | Password portal |
| mDNS | `http://esp32sensor.local` | Akses portal via browser |
| Server URL | `https://ozora.b4its.cloud` | Endpoint Django |
| TCS Gain | 4x | Gain sensor warna |
| Integration Time | 50 ms | Waktu integrasi TCS34725 |
