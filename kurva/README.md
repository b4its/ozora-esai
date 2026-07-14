# Color Simulation - Rhemazol Yellow FG

Program simulasi dan visualisasi data sensor warna untuk cairan Rhemazol Yellow FG dengan konsentrasi 45% hingga 0% (putih/jernih).

## Parameter yang Divisualisasikan

| Parameter | Deskripsi | Range | Satuan |
|-----------|-----------|-------|--------|
| **Red (R)** | Komponen warna merah | 0-255 | - |
| **Green (G)** | Komponen warna hijau | 0-255 | - |
| **Blue (B)** | Komponen warna biru | 0-255 | - |
| **Lux** | Intensitas cahaya | 10-150 | lm/m² |
| **Color Temperature** | Suhu warna | 2500-7000 | Kelvin (K) |

## Karakteristik Rhemazol Yellow FG

### Konsentrasi Tinggi (45%)
- Warna: Kuning pekat
- RGB: R tinggi (~200), G tinggi (~180), B rendah (~50)
- Lux: Rendah (~30 lm/m²) - banyak cahaya diserap
- Color Temperature: Warm (~3000K)

### Konsentrasi Rendah (0%)
- Warna: Putih/jernih
- RGB: R, G, B seimbang tinggi (~255)
- Lux: Tinggi (~150 lm/m²) - cahaya tidak diserap
- Color Temperature: Neutral (~6500K)

## Cara Menjalankan

```bash
cd kurva
python color_simulation.py
```

## Dependencies

```bash
pip install numpy matplotlib
```

## Output

Program menghasilkan:

1. **Tabel Data** - Ditampilkan di terminal:
   ```
   Konsentrasi | Red | Green | Blue | Lux | Color Temp
   ```

2. **Grafik** - Disimpan di `output/rhemazol_yellow_fg_analysis.png`:
   - Kurva RGB vs Konsentrasi
   - Kurva Lux & Color Temperature (dual axis)
   - Kurva Color Temperature dengan zona (Warm/Neutral/Cool)
   - Panel Statistik Data
   - Visualisasi Gradient Warna Aktual

## Struktur Output Grafik

```
┌─────────────────────────────────────────────────────────────┐
│         Analisis Sensor Warna: Rhemazol Yellow FG           │
│       Konsentrasi 45% (Kuning Pekat) → 0% (Putih/Jernih)    │
├─────────────────────────────┬───────────────────────────────┤
│                             │                               │
│         Kurva RGB           │   Kurva Lux & Color Temp      │
│      (R, G, B lines)        │       (dual y-axis)           │
│                             │                               │
├─────────────────────────────┼───────────────────────────────┤
│                             │                               │
│   Kurva Color Temperature   │      Panel Statistik          │
│  (dengan zona Warm/Cool)    │         (Info Box)            │
│                             │                               │
├─────────────────────────────┴───────────────────────────────┤
│        Visualisasi Warna Aktual (45% → 0%)                  │
│          [Kuning Pekat]  ─────────────  [Putih Jernih]      │
└─────────────────────────────────────────────────────────────┘
```

## Fungsi Utama

| Fungsi | Deskripsi |
|--------|-----------|
| `generate_rhemazol_data()` | Generate data simulasi sensor warna |
| `plot_rgb_curve()` | Plot kurva RGB dengan marker |
| `plot_lux_curve()` | Plot kurva Lux |
| `plot_color_temperature_curve()` | Plot kurva Color Temperature dengan zona |
| `plot_lux_and_colortemp()` | Plot Lux & Color Temp dalam satu grafik (dual axis) |
| `create_color_gradient_bar()` | Buat gradient bar visualisasi warna |
| `create_info_panel()` | Tampilkan panel statistik |
| `print_data_table()` | Print tabel data ke terminal |

## Contoh Penggunaan dalam Kode Lain

```python
from color_simulation import generate_rhemazol_data

# Generate data
data = generate_rhemazol_data(num_points=46)

# Akses data
print(data['concentration'])      # Array konsentrasi (45 → 0)
print(data['red'])                # Array nilai Red
print(data['green'])              # Array nilai Green
print(data['blue'])               # Array nilai Blue
print(data['lux'])                # Array nilai Lux
print(data['color_temperature'])  # Array Color Temperature
```

## Interpretasi Kurva

1. **Kurva RGB**: Menunjukkan perubahan komponen warna seiring penurunan konsentrasi. Blue meningkat drastis karena warna kuning menyerap spektrum biru.

2. **Kurva Lux & Color Temperature**: Menampilkan dua parameter dalam satu grafik dengan dual y-axis. Keduanya meningkat seiring penurunan konsentrasi.

3. **Kurva Color Temperature**: Meningkat dari warm (~3000K) ke neutral (~6500K) seiring penurunan konsentrasi. Zona warna (Warm/Neutral/Cool) ditampilkan sebagai background.

4. **Gradient Bar**: Menampilkan warna aktual dari kuning pekat (kiri) ke putih jernih (kanan).

---

## Glosarium

### Istilah Umum

| Istilah | Definisi |
|---------|----------|
| **Rhemazol Yellow FG** | Zat pewarna reaktif berwarna kuning yang digunakan dalam industri tekstil. Memiliki sifat larut dalam air dan menghasilkan warna kuning cerah. |
| **Konsentrasi** | Jumlah zat terlarut (Rhemazol Yellow FG) dalam larutan, dinyatakan dalam persen (%). Semakin tinggi konsentrasi, semakin pekat warnanya. |
| **Simulasi** | Proses pemodelan data berdasarkan karakteristik nyata untuk memprediksi perilaku sistem tanpa melakukan eksperimen fisik. |
| **Sensor Warna** | Perangkat elektronik yang mendeteksi dan mengukur komponen warna (RGB), intensitas cahaya (Lux), dan suhu warna (Color Temperature). |
| **Analisis** | Proses penguraian data menjadi komponen-komponen untuk memahami hubungan dan pola yang ada. |

### Parameter Warna (RGB)

| Istilah | Definisi |
|---------|----------|
| **RGB** | Model warna aditif yang terdiri dari tiga komponen: Red (merah), Green (hijau), dan Blue (biru). Kombinasi ketiga warna ini dapat menghasilkan spektrum warna yang luas. |
| **Red (R)** | Komponen warna merah dalam model RGB. Nilai 0-255, di mana 0 = tidak ada merah, 255 = merah maksimum. |
| **Green (G)** | Komponen warna hijau dalam model RGB. Nilai 0-255, di mana 0 = tidak ada hijau, 255 = hijau maksimum. |
| **Blue (B)** | Komponen warna biru dalam model RGB. Nilai 0-255, di mana 0 = tidak ada biru, 255 = biru maksimum. |
| **Nilai RGB (0-255)** | Rentang nilai untuk setiap komponen warna dalam sistem 8-bit. 0 berarti tidak ada intensitas, 255 berarti intensitas maksimum. |
| **Absorpsi Spektrum** | Proses di mana suatu zat menyerap panjang gelombang cahaya tertentu. Rhemazol Yellow FG menyerap spektrum biru sehingga terlihat kuning. |

### Parameter Cahaya

| Istilah | Definisi |
|---------|----------|
| **Lux** | Satuan ukur intensitas cahaya (iluminasi) yang diterima per satuan luas. 1 lux = 1 lumen per meter persegi (lm/m²). |
| **Lumen (lm)** | Satuan ukur fluks cahaya yang dipancarkan oleh sumber cahaya. |
| **Intensitas Cahaya** | Jumlah cahaya yang melewati atau dipantulkan oleh suatu medium. Cairan pekat menyerap lebih banyak cahaya sehingga intensitasnya lebih rendah. |
| **lm/m²** | Lumen per meter persegi, satuan yang sama dengan Lux. |

### Parameter Suhu Warna

| Istilah | Definisi |
|---------|----------|
| **Color Temperature** | Karakteristik visual cahaya yang diukur dalam Kelvin (K). Menggambarkan "kehangatan" atau "kedinginan" warna cahaya. |
| **Kelvin (K)** | Satuan suhu dalam Sistem Internasional (SI). Dalam konteks warna, digunakan untuk mengukur suhu warna. |
| **Warm (Hangat)** | Suhu warna rendah (2500-4000K). Menghasilkan warna kekuningan/keoranyean, seperti cahaya lilin atau matahari terbenam. |
| **Neutral (Netral)** | Suhu warna menengah (4000-5500K). Menghasilkan warna putih natural, seperti cahaya matahari siang hari. |
| **Cool (Dingin)** | Suhu warna tinggi (5500-7000K). Menghasilkan warna kebiruan, seperti cahaya langit biru atau lampu LED putih. |

### Istilah pada Grafik

| Istilah | Definisi |
|---------|----------|
| **Kurva** | Grafik garis yang menunjukkan hubungan antara dua variabel, misalnya konsentrasi vs nilai RGB. |
| **Kurva RGB** | Grafik yang menampilkan perubahan nilai Red, Green, dan Blue terhadap konsentrasi. |
| **Kurva Lux** | Grafik yang menampilkan perubahan intensitas cahaya (Lux) terhadap konsentrasi. |
| **Kurva Color Temperature** | Grafik yang menampilkan perubahan suhu warna terhadap konsentrasi. |
| **Dual Y-Axis** | Grafik dengan dua sumbu Y (kiri dan kanan) untuk menampilkan dua parameter dengan satuan berbeda dalam satu grafik. |
| **Zona Warna** | Area berwarna pada grafik Color Temperature yang menandai rentang Warm (orange), Neutral (kuning), dan Cool (biru muda). |
| **Gradient Bar** | Visualisasi berupa bar horizontal yang menampilkan transisi warna secara bertahap dari kuning pekat ke putih jernih. |
| **Visualisasi Warna Aktual** | Representasi visual warna sebenarnya berdasarkan nilai RGB pada setiap titik konsentrasi. |
| **Marker** | Simbol (lingkaran, kotak, segitiga, diamond, pentagon) pada titik data di grafik untuk memudahkan identifikasi setiap parameter. |
| **Legend** | Keterangan pada grafik yang menjelaskan arti setiap garis atau simbol yang digunakan. |
| **Panel Statistik** | Kotak informasi yang menampilkan ringkasan data numerik seperti nilai RGB, Lux, dan Color Temperature pada titik tertentu. |
| **Konsentrasi (%)** | Label sumbu X yang menunjukkan persentase kandungan Rhemazol Yellow FG dalam larutan. |

### Istilah Warna pada Visualisasi

| Istilah | Definisi |
|---------|----------|
| **Kuning Pekat** | Warna larutan pada konsentrasi tinggi (45%), ditandai dengan R dan G tinggi, B rendah. |
| **Putih Jernih** | Warna larutan pada konsentrasi 0%, ditandai dengan R, G, B seimbang tinggi (~255). |
| **Fill Between** | Area yang diarsir di bawah kurva untuk memperjelas tren dan memudahkan pembacaan grafik. |

### Istilah Pemrograman

| Istilah | Definisi |
|---------|----------|
| **NumPy** | Library Python untuk komputasi numerik, menyediakan array multidimensi dan fungsi matematika. |
| **Matplotlib** | Library Python untuk membuat visualisasi dan grafik 2D. |
| **GridSpec** | Modul Matplotlib untuk mengatur tata letak subplot secara fleksibel dalam satu figure. |
| **Figure** | Objek utama dalam Matplotlib yang berisi seluruh elemen grafik (subplot, judul, dll). |
| **Subplot** | Grafik individual dalam satu figure yang dapat disusun dalam grid. |
| **Axes** | Area dalam subplot tempat data divisualisasikan, termasuk sumbu X dan Y. |
| **DPI (Dots Per Inch)** | Resolusi gambar yang menentukan kualitas output. Semakin tinggi DPI, semakin detail gambar. |
| **Random Seed** | Nilai awal untuk generator angka acak. Menggunakan seed yang sama menghasilkan data acak yang identik (reproducibility). |

### Istilah Statistik

| Istilah | Definisi |
|---------|----------|
| **Min (Minimum)** | Nilai terkecil dalam sekumpulan data. |
| **Max (Maximum)** | Nilai terbesar dalam sekumpulan data. |
| **Range** | Selisih antara nilai maksimum dan minimum dalam sekumpulan data. |
