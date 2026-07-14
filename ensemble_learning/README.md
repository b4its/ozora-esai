# Ensemble Learning - Color & Sterile Classification

Modul machine learning untuk prediksi warna dan sterilitas air menggunakan data sensor TCS34725 pada ESP32.

## Model yang Tersedia

| Model | Algoritma | Fungsi | File |
|-------|-----------|--------|------|
| **Color Classifier** | Random Forest | Memprediksi warna cairan | `color_classifier.py` |
| **Sterile Classifier** | XGBoost | Memprediksi apakah air sudah steril/jernih | `sterile_classifier.py` |

## Input Data (Sensor TCS34725)

| Parameter | Deskripsi | Range | Satuan |
|-----------|-----------|-------|--------|
| **red** | Komponen warna merah | 0-255 (atau raw) | - |
| **green** | Komponen warna hijau | 0-255 (atau raw) | - |
| **blue** | Komponen warna biru | 0-255 (atau raw) | - |
| **lux** | Intensitas cahaya | 0-200 | lm/m² |
| **raw_light** | Nilai raw dari sensor | 0-2000 | - |
| **color_temp** | Suhu warna | 2000-10000 | Kelvin (K) |

## Instalasi Dependencies

```bash
pip install numpy pandas scikit-learn xgboost matplotlib seaborn joblib
```

## Cara Menjalankan

### 1. Training Color Classifier (Random Forest)

```bash
cd ensemble_learning
python color_classifier.py
```

Output:
- Model: `models/color_classifier_rf.joblib`
- Plot: `output/color_feature_importance.png`, `output/color_confusion_matrix.png`

### 2. Training Sterile Classifier (XGBoost)

```bash
cd ensemble_learning
python sterile_classifier.py
```

Output:
- Model: `models/sterile_classifier_xgb.joblib`
- Plot: `output/sterile_feature_importance.png`, `output/sterile_confusion_matrix.png`, `output/sterile_roc_curve.png`

## Penggunaan dalam Kode

### Color Classifier

```python
from color_classifier import ColorClassifier

# Load model
classifier = ColorClassifier()
classifier.load_model('models/color_classifier_rf.joblib')

# Prediksi single reading
result = classifier.predict_single(
    red=200, green=180, blue=50,
    lux=35, color_temp=3000, raw_light=400
)

print(result['predicted_color'])  # 'kuning_pekat'
print(result['confidence'])       # 0.95
print(result['probabilities'])    # {'kuning_pekat': 0.95, 'kuning_muda': 0.03, ...}
```

### Sterile Classifier

```python
from sterile_classifier import SterileClassifier

# Load model
classifier = SterileClassifier()
classifier.load_model('models/sterile_classifier_xgb.joblib')

# Prediksi single reading
result = classifier.predict_single(
    red=250, green=248, blue=252,
    lux=130, raw_light=950
)

print(result['is_sterile'])           # True
print(result['confidence'])           # 0.98
print(result['probability_sterile'])  # 0.98
print(result['analysis'])             # {'lux_ok': True, 'raw_ok': True, 'balance_ok': True, ...}
```

## Struktur Folder

```
ensemble_learning/
├── color_classifier.py      # Random Forest untuk klasifikasi warna
├── sterile_classifier.py    # XGBoost untuk klasifikasi steril
├── README.md                # Dokumentasi ini
├── models/                  # Model yang sudah di-training
│   ├── color_classifier_rf.joblib
│   └── sterile_classifier_xgb.joblib
└── output/                  # Output grafik dan visualisasi
    ├── color_feature_importance.png
    ├── color_confusion_matrix.png
    ├── sterile_feature_importance.png
    ├── sterile_confusion_matrix.png
    └── sterile_roc_curve.png
```

## Detail Model

### 1. Color Classifier (Random Forest)

**Algoritma:** Random Forest Classifier
- `n_estimators`: 100
- `max_depth`: 10
- `min_samples_split`: 5
- `min_samples_leaf`: 2

**Features:**
- red, green, blue (RGB)
- lux (intensitas cahaya)
- color_temp (suhu warna)
- raw_light (nilai raw sensor)

**Classes (10 warna):**
- `kuning_pekat` - Rhemazol Yellow FG konsentrasi tinggi
- `kuning_muda` - Rhemazol Yellow FG konsentrasi rendah
- `putih` - Air jernih/steril
- `merah`, `hijau`, `biru`, `oranye`, `ungu`, `coklat`, `abu_abu`

### 2. Sterile Classifier (XGBoost)

**Algoritma:** XGBoost Classifier
- `n_estimators`: 100
- `max_depth`: 6
- `learning_rate`: 0.1
- `subsample`: 0.8
- `colsample_bytree`: 0.8

**Features:**
- red, green, blue (RGB)
- lux (intensitas cahaya)
- raw_light (nilai raw sensor)
- rgb_balance (derived: keseimbangan RGB)
- r_ratio, g_ratio, b_ratio (derived: rasio komponen)

**Classes (Binary):**
- `0` - Tidak steril (air keruh/berwarna)
- `1` - Steril (air jernih/bersih)

**Kriteria Steril (threshold default):**
- Lux >= 300 lm/m²
- Raw Light >= 800
- RGB Balance <= 0.15 (seimbang)

## Integrasi dengan Web Ozora

Model ini dapat diintegrasikan dengan endpoint Django di `web_ozora`:

```python
# views.py - contoh integrasi
from ensemble_learning.color_classifier import ColorClassifier
from ensemble_learning.sterile_classifier import SterileClassifier

# Load models saat startup
color_clf = ColorClassifier()
color_clf.load_model('path/to/color_classifier_rf.joblib')

sterile_clf = SterileClassifier()
sterile_clf.load_model('path/to/sterile_classifier_xgb.joblib')

@api_view(['POST'])
def predict_color(request):
    data = request.data
    result = color_clf.predict_single(
        red=data['red'], green=data['green'], blue=data['blue'],
        lux=data['lux'], color_temp=data.get('temp', 0),
        raw_light=data.get('raw_light', 0)
    )
    return Response(result)

@api_view(['POST'])
def predict_sterile(request):
    data = request.data
    result = sterile_clf.predict_single(
        red=data['red'], green=data['green'], blue=data['blue'],
        lux=data['lux'], raw_light=data.get('raw_light', 0)
    )
    return Response(result)
```

## Glosarium

### Istilah Machine Learning

| Istilah | Definisi |
|---------|----------|
| **Ensemble Learning** | Teknik yang menggabungkan beberapa model untuk meningkatkan akurasi prediksi. |
| **Random Forest** | Algoritma ensemble yang menggunakan banyak decision tree dan mengambil voting mayoritas. |
| **XGBoost** | Extreme Gradient Boosting, algoritma boosting yang mengoptimalkan decision tree secara iteratif. |
| **Feature Importance** | Ukuran seberapa penting setiap fitur dalam membuat prediksi. |
| **Cross-Validation** | Teknik validasi model dengan membagi data menjadi beberapa fold. |
| **Confusion Matrix** | Tabel yang menunjukkan perbandingan prediksi vs aktual. |
| **ROC Curve** | Kurva yang menunjukkan trade-off antara True Positive Rate dan False Positive Rate. |
| **AUC (Area Under Curve)** | Metrik evaluasi, semakin mendekati 1 semakin baik. |

### Istilah Klasifikasi

| Istilah | Definisi |
|---------|----------|
| **Accuracy** | Persentase prediksi yang benar dari total prediksi. |
| **Precision** | Dari semua yang diprediksi positif, berapa yang benar-benar positif. |
| **Recall** | Dari semua yang sebenarnya positif, berapa yang berhasil diprediksi positif. |
| **F1-Score** | Harmonic mean dari Precision dan Recall. |
| **Confidence** | Tingkat keyakinan model terhadap prediksi. |
| **Probability** | Probabilitas untuk setiap kelas output. |

### Istilah Sensor

| Istilah | Definisi |
|---------|----------|
| **TCS34725** | Sensor warna RGB dengan light-to-digital converter. |
| **RGB** | Model warna Red, Green, Blue. |
| **Lux** | Satuan intensitas cahaya (lumen per meter persegi). |
| **Raw Light** | Nilai mentah dari sensor sebelum dikonversi. |
| **Color Temperature** | Suhu warna dalam Kelvin. |
| **RGB Balance** | Keseimbangan antara komponen RGB, dihitung dari selisih rasio max dan min. |

### Istilah Sterilitas

| Istilah | Definisi |
|---------|----------|
| **Steril** | Kondisi air yang sudah jernih dan bersih. |
| **Tidak Steril** | Kondisi air yang masih keruh atau berwarna. |
| **Threshold** | Nilai batas untuk menentukan kondisi steril. |
| **RGB Balance Threshold** | Batas maksimal selisih rasio RGB (default 0.15). |
| **Lux Threshold** | Batas minimal intensitas cahaya untuk dianggap steril (default 300). |
| **Raw Threshold** | Batas minimal nilai raw sensor untuk dianggap steril (default 800). |
