"""
Sterile Classifier menggunakan XGBoost
======================================

Model ini memprediksi apakah air sudah steril/jernih berdasarkan data sensor:
- Red (R): Nilai komponen merah
- Green (G): Nilai komponen hijau
- Blue (B): Nilai komponen biru
- Lux: Intensitas cahaya
- Raw Light: Nilai raw dari sensor

Kriteria Steril (berdasarkan logic di web_ozora):
- Lux >= threshold (default 300)
- Raw Light >= threshold (default 800)
- RGB Balance <= threshold (default 0.15)
  Balance = max(r_ratio, g_ratio, b_ratio) - min(r_ratio, g_ratio, b_ratio)

Input dari sensor TCS34725 pada ESP32.
"""

import os
import numpy as np
import pandas as pd
import joblib
from xgboost import XGBClassifier
from sklearn.model_selection import train_test_split, cross_val_score, GridSearchCV
from sklearn.metrics import (
    classification_report, confusion_matrix, accuracy_score,
    precision_score, recall_score, f1_score, roc_auc_score, roc_curve
)
from sklearn.preprocessing import StandardScaler
import matplotlib.pyplot as plt
import seaborn as sns


class SterileClassifier:
    """
    XGBoost Classifier untuk memprediksi apakah air sudah steril/jernih.
    
    Features:
    - red: Nilai komponen merah
    - green: Nilai komponen hijau
    - blue: Nilai komponen biru
    - lux: Intensitas cahaya
    - raw_light: Nilai raw dari sensor
    - rgb_balance: Keseimbangan RGB (derived feature)
    - r_ratio, g_ratio, b_ratio: Rasio masing-masing komponen
    
    Labels:
    - 0: Tidak steril (air masih keruh/berwarna)
    - 1: Steril (air jernih/bersih)
    """
    
    def __init__(self, random_state=42):
        """
        Inisialisasi model XGBoost.
        
        Parameters:
        -----------
        random_state : int
            Seed untuk reproducibility
        """
        self.model = XGBClassifier(
            n_estimators=100,
            max_depth=6,
            learning_rate=0.1,
            subsample=0.8,
            colsample_bytree=0.8,
            random_state=random_state,
            use_label_encoder=False,
            eval_metric='logloss'
        )
        self.scaler = StandardScaler()
        self.is_fitted = False
        self.feature_names = [
            'red', 'green', 'blue', 'lux', 'raw_light',
            'rgb_balance', 'r_ratio', 'g_ratio', 'b_ratio'
        ]
        
        # Default thresholds (dari web_ozora IoTDevice model)
        self.sterile_lux_min = 300.0
        self.sterile_raw_min = 800.0
        self.sterile_balance_max = 0.15
    
    def calculate_derived_features(self, red, green, blue) -> tuple:
        """
        Hitung fitur turunan dari RGB.
        
        Parameters:
        -----------
        red, green, blue : array-like
            Nilai RGB
        
        Returns:
        --------
        tuple : (rgb_balance, r_ratio, g_ratio, b_ratio)
        """
        red = np.array(red, dtype=float)
        green = np.array(green, dtype=float)
        blue = np.array(blue, dtype=float)
        
        total = red + green + blue
        # Hindari division by zero
        total = np.where(total == 0, 1, total)
        
        r_ratio = red / total
        g_ratio = green / total
        b_ratio = blue / total
        
        # RGB Balance: selisih antara max dan min ratio
        ratios = np.column_stack([r_ratio, g_ratio, b_ratio])
        rgb_balance = np.max(ratios, axis=1) - np.min(ratios, axis=1)
        
        return rgb_balance, r_ratio, g_ratio, b_ratio
    
    def generate_training_data(self, n_samples=2000) -> pd.DataFrame:
        """
        Generate data training simulasi untuk klasifikasi steril.
        
        Parameters:
        -----------
        n_samples : int
            Jumlah sampel per kelas
        
        Returns:
        --------
        pd.DataFrame : Dataset dengan features dan label
        """
        np.random.seed(42)
        data = []
        
        # === Data STERIL (air jernih) ===
        # Karakteristik: RGB seimbang tinggi, Lux tinggi, Raw tinggi
        for _ in range(n_samples):
            # RGB seimbang dan tinggi (putih/jernih)
            base = np.random.uniform(230, 255)
            red = base + np.random.normal(0, 5)
            green = base + np.random.normal(0, 5)
            blue = base + np.random.normal(0, 5)
            
            # Lux tinggi (cahaya menembus)
            lux = np.random.uniform(100, 150) + np.random.normal(0, 5)
            
            # Raw light tinggi
            raw_light = np.random.uniform(800, 1200) + np.random.normal(0, 20)
            
            # Clip values
            red = np.clip(red, 0, 255)
            green = np.clip(green, 0, 255)
            blue = np.clip(blue, 0, 255)
            lux = np.clip(lux, 0, 200)
            raw_light = np.clip(raw_light, 0, 2000)
            
            data.append({
                'red': red, 'green': green, 'blue': blue,
                'lux': lux, 'raw_light': raw_light,
                'is_sterile': 1
            })
        
        # === Data TIDAK STERIL (berbagai kondisi) ===
        
        # 1. Air berwarna kuning (Rhemazol Yellow FG)
        for _ in range(n_samples // 3):
            red = np.random.uniform(180, 220) + np.random.normal(0, 5)
            green = np.random.uniform(160, 200) + np.random.normal(0, 5)
            blue = np.random.uniform(30, 100) + np.random.normal(0, 5)
            lux = np.random.uniform(20, 60) + np.random.normal(0, 3)
            raw_light = np.random.uniform(300, 600) + np.random.normal(0, 15)
            
            data.append({
                'red': np.clip(red, 0, 255),
                'green': np.clip(green, 0, 255),
                'blue': np.clip(blue, 0, 255),
                'lux': np.clip(lux, 0, 200),
                'raw_light': np.clip(raw_light, 0, 2000),
                'is_sterile': 0
            })
        
        # 2. Air keruh (warna abu-abu tidak seimbang)
        for _ in range(n_samples // 3):
            base = np.random.uniform(80, 150)
            red = base + np.random.uniform(-30, 30) + np.random.normal(0, 10)
            green = base + np.random.uniform(-30, 30) + np.random.normal(0, 10)
            blue = base + np.random.uniform(-30, 30) + np.random.normal(0, 10)
            lux = np.random.uniform(30, 80) + np.random.normal(0, 5)
            raw_light = np.random.uniform(400, 700) + np.random.normal(0, 20)
            
            data.append({
                'red': np.clip(red, 0, 255),
                'green': np.clip(green, 0, 255),
                'blue': np.clip(blue, 0, 255),
                'lux': np.clip(lux, 0, 200),
                'raw_light': np.clip(raw_light, 0, 2000),
                'is_sterile': 0
            })
        
        # 3. Air dengan warna lain (merah, hijau, coklat, dll)
        for _ in range(n_samples // 3 + n_samples % 3):
            color_type = np.random.choice(['merah', 'hijau', 'coklat', 'oranye'])
            
            if color_type == 'merah':
                red = np.random.uniform(180, 255)
                green = np.random.uniform(20, 80)
                blue = np.random.uniform(20, 80)
            elif color_type == 'hijau':
                red = np.random.uniform(20, 80)
                green = np.random.uniform(150, 230)
                blue = np.random.uniform(20, 100)
            elif color_type == 'coklat':
                red = np.random.uniform(120, 180)
                green = np.random.uniform(80, 140)
                blue = np.random.uniform(40, 100)
            else:  # oranye
                red = np.random.uniform(200, 255)
                green = np.random.uniform(100, 180)
                blue = np.random.uniform(20, 80)
            
            red += np.random.normal(0, 5)
            green += np.random.normal(0, 5)
            blue += np.random.normal(0, 5)
            lux = np.random.uniform(25, 70) + np.random.normal(0, 5)
            raw_light = np.random.uniform(300, 600) + np.random.normal(0, 15)
            
            data.append({
                'red': np.clip(red, 0, 255),
                'green': np.clip(green, 0, 255),
                'blue': np.clip(blue, 0, 255),
                'lux': np.clip(lux, 0, 200),
                'raw_light': np.clip(raw_light, 0, 2000),
                'is_sterile': 0
            })
        
        df = pd.DataFrame(data)
        
        # Tambahkan derived features
        rgb_balance, r_ratio, g_ratio, b_ratio = self.calculate_derived_features(
            df['red'], df['green'], df['blue']
        )
        df['rgb_balance'] = rgb_balance
        df['r_ratio'] = r_ratio
        df['g_ratio'] = g_ratio
        df['b_ratio'] = b_ratio
        
        return df
    
    def prepare_features(self, df: pd.DataFrame) -> np.ndarray:
        """
        Menyiapkan feature matrix dari DataFrame.
        
        Parameters:
        -----------
        df : pd.DataFrame
            DataFrame dengan kolom sensor
        
        Returns:
        --------
        np.ndarray : Feature matrix
        """
        # Hitung derived features jika belum ada
        if 'rgb_balance' not in df.columns:
            rgb_balance, r_ratio, g_ratio, b_ratio = self.calculate_derived_features(
                df['red'], df['green'], df['blue']
            )
            df = df.copy()
            df['rgb_balance'] = rgb_balance
            df['r_ratio'] = r_ratio
            df['g_ratio'] = g_ratio
            df['b_ratio'] = b_ratio
        
        # Ambil features
        features = []
        for col in self.feature_names:
            if col in df.columns:
                features.append(df[col].values)
            else:
                features.append(np.zeros(len(df)))
        
        X = np.column_stack(features)
        return X
    
    def fit(self, X, y):
        """
        Training model XGBoost.
        
        Parameters:
        -----------
        X : array-like
            Feature matrix (n_samples, n_features)
        y : array-like
            Label (0 = tidak steril, 1 = steril)
        """
        # Scale features
        X_scaled = self.scaler.fit_transform(X)
        
        # Fit model
        self.model.fit(X_scaled, y)
        self.is_fitted = True
        
        print(f"Model trained with {len(X)} samples")
        print(f"Class distribution: {np.bincount(y)}")
        return self
    
    def predict(self, X) -> np.ndarray:
        """
        Prediksi apakah air steril.
        
        Parameters:
        -----------
        X : array-like
            Feature matrix
        
        Returns:
        --------
        np.ndarray : Predicted labels (0 atau 1)
        """
        if not self.is_fitted:
            raise ValueError("Model belum di-training. Panggil fit() terlebih dahulu.")
        
        X_scaled = self.scaler.transform(X)
        return self.model.predict(X_scaled)
    
    def predict_proba(self, X) -> np.ndarray:
        """
        Prediksi probabilitas steril.
        
        Parameters:
        -----------
        X : array-like
            Feature matrix
        
        Returns:
        --------
        np.ndarray : Probabilitas [P(tidak steril), P(steril)]
        """
        if not self.is_fitted:
            raise ValueError("Model belum di-training.")
        
        X_scaled = self.scaler.transform(X)
        return self.model.predict_proba(X_scaled)
    
    def predict_single(self, red, green, blue, lux, raw_light) -> dict:
        """
        Prediksi sterilitas dari single sensor reading.
        
        Parameters:
        -----------
        red, green, blue : float
            Nilai RGB
        lux : float
            Intensitas cahaya
        raw_light : float
            Nilai raw sensor
        
        Returns:
        --------
        dict : Hasil prediksi dengan probabilitas dan analisis
        """
        # Hitung derived features
        rgb_balance, r_ratio, g_ratio, b_ratio = self.calculate_derived_features(
            [red], [green], [blue]
        )
        
        X = np.array([[
            red, green, blue, lux, raw_light,
            rgb_balance[0], r_ratio[0], g_ratio[0], b_ratio[0]
        ]])
        
        prediction = self.predict(X)[0]
        probabilities = self.predict_proba(X)[0]
        
        # Analisis berdasarkan threshold
        analysis = {
            'lux_ok': lux >= self.sterile_lux_min,
            'raw_ok': raw_light >= self.sterile_raw_min,
            'balance_ok': rgb_balance[0] <= self.sterile_balance_max,
            'rgb_balance': rgb_balance[0],
            'thresholds': {
                'lux_min': self.sterile_lux_min,
                'raw_min': self.sterile_raw_min,
                'balance_max': self.sterile_balance_max
            }
        }
        
        return {
            'is_sterile': bool(prediction),
            'confidence': probabilities[prediction],
            'probability_sterile': probabilities[1],
            'probability_not_sterile': probabilities[0],
            'analysis': analysis
        }
    
    def evaluate(self, X_test, y_test) -> dict:
        """
        Evaluasi model dengan test data.
        
        Parameters:
        -----------
        X_test : array-like
            Test features
        y_test : array-like
            True labels
        
        Returns:
        --------
        dict : Metrics evaluasi
        """
        y_pred = self.predict(X_test)
        y_proba = self.predict_proba(X_test)[:, 1]
        
        metrics = {
            'accuracy': accuracy_score(y_test, y_pred),
            'precision': precision_score(y_test, y_pred),
            'recall': recall_score(y_test, y_pred),
            'f1_score': f1_score(y_test, y_pred),
            'roc_auc': roc_auc_score(y_test, y_proba),
            'confusion_matrix': confusion_matrix(y_test, y_pred),
            'classification_report': classification_report(y_test, y_pred, output_dict=True),
            'predictions': y_pred,
            'probabilities': y_proba
        }
        
        return metrics
    
    def get_feature_importance(self) -> pd.DataFrame:
        """
        Mendapatkan feature importance dari model.
        
        Returns:
        --------
        pd.DataFrame : Feature importance ranking
        """
        if not self.is_fitted:
            raise ValueError("Model belum di-training.")
        
        importance = pd.DataFrame({
            'feature': self.feature_names,
            'importance': self.model.feature_importances_
        }).sort_values('importance', ascending=False)
        
        return importance
    
    def plot_feature_importance(self, save_path=None):
        """Plot feature importance."""
        importance = self.get_feature_importance()
        
        fig, ax = plt.subplots(figsize=(10, 6))
        colors = plt.cm.viridis(np.linspace(0.2, 0.8, len(importance)))
        bars = ax.barh(importance['feature'], importance['importance'], color=colors)
        ax.set_xlabel('Importance')
        ax.set_title('Feature Importance - XGBoost Sterile Classifier')
        ax.invert_yaxis()
        
        # Add value labels
        for bar, val in zip(bars, importance['importance']):
            ax.text(val + 0.005, bar.get_y() + bar.get_height()/2, 
                   f'{val:.3f}', va='center', fontsize=9)
        
        plt.tight_layout()
        
        if save_path:
            plt.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"Feature importance plot saved to: {save_path}")
        
        plt.show()
        return fig
    
    def plot_confusion_matrix(self, y_test, y_pred, save_path=None):
        """Plot confusion matrix."""
        cm = confusion_matrix(y_test, y_pred)
        
        fig, ax = plt.subplots(figsize=(8, 6))
        sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                   xticklabels=['Tidak Steril', 'Steril'],
                   yticklabels=['Tidak Steril', 'Steril'], ax=ax,
                   annot_kws={'size': 14})
        ax.set_xlabel('Predicted', fontsize=12)
        ax.set_ylabel('Actual', fontsize=12)
        ax.set_title('Confusion Matrix - Sterile Classifier', fontsize=14)
        
        plt.tight_layout()
        
        if save_path:
            plt.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"Confusion matrix saved to: {save_path}")
        
        plt.show()
        return fig
    
    def plot_roc_curve(self, y_test, y_proba, save_path=None):
        """Plot ROC curve."""
        fpr, tpr, thresholds = roc_curve(y_test, y_proba)
        roc_auc = roc_auc_score(y_test, y_proba)
        
        fig, ax = plt.subplots(figsize=(8, 6))
        ax.plot(fpr, tpr, color='steelblue', lw=2, 
                label=f'ROC Curve (AUC = {roc_auc:.3f})')
        ax.plot([0, 1], [0, 1], color='gray', lw=1, linestyle='--', label='Random')
        ax.fill_between(fpr, tpr, alpha=0.2, color='steelblue')
        
        ax.set_xlabel('False Positive Rate', fontsize=12)
        ax.set_ylabel('True Positive Rate', fontsize=12)
        ax.set_title('ROC Curve - Sterile Classifier', fontsize=14)
        ax.legend(loc='lower right')
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        if save_path:
            plt.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"ROC curve saved to: {save_path}")
        
        plt.show()
        return fig
    
    def save_model(self, filepath: str):
        """
        Simpan model ke file.
        
        Parameters:
        -----------
        filepath : str
            Path untuk menyimpan model
        """
        model_data = {
            'model': self.model,
            'scaler': self.scaler,
            'feature_names': self.feature_names,
            'is_fitted': self.is_fitted,
            'thresholds': {
                'sterile_lux_min': self.sterile_lux_min,
                'sterile_raw_min': self.sterile_raw_min,
                'sterile_balance_max': self.sterile_balance_max
            }
        }
        joblib.dump(model_data, filepath)
        print(f"Model saved to: {filepath}")
    
    def load_model(self, filepath: str):
        """
        Load model dari file.
        
        Parameters:
        -----------
        filepath : str
            Path file model
        """
        model_data = joblib.load(filepath)
        self.model = model_data['model']
        self.scaler = model_data['scaler']
        self.feature_names = model_data['feature_names']
        self.is_fitted = model_data['is_fitted']
        
        if 'thresholds' in model_data:
            self.sterile_lux_min = model_data['thresholds']['sterile_lux_min']
            self.sterile_raw_min = model_data['thresholds']['sterile_raw_min']
            self.sterile_balance_max = model_data['thresholds']['sterile_balance_max']
        
        print(f"Model loaded from: {filepath}")


def train_and_save_model():
    """
    Training model dan simpan ke file.
    """
    print("="*60)
    print("TRAINING STERILE CLASSIFIER (XGBoost)")
    print("="*60)
    
    # Inisialisasi classifier
    classifier = SterileClassifier(random_state=42)
    
    # Generate training data
    print("\n[1] Generating training data...")
    df = classifier.generate_training_data(n_samples=1500)
    print(f"    Total samples: {len(df)}")
    print(f"    Steril: {(df['is_sterile']==1).sum()}, Tidak Steril: {(df['is_sterile']==0).sum()}")
    
    # Prepare features dan labels
    X = classifier.prepare_features(df)
    y = df['is_sterile'].values
    
    # Split data
    print("\n[2] Splitting data (80% train, 20% test)...")
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )
    print(f"    Train samples: {len(X_train)}")
    print(f"    Test samples: {len(X_test)}")
    
    # Training
    print("\n[3] Training XGBoost model...")
    classifier.fit(X_train, y_train)
    
    # Cross-validation
    print("\n[4] Cross-validation (5-fold)...")
    X_train_scaled = classifier.scaler.transform(X_train)
    cv_scores = cross_val_score(classifier.model, X_train_scaled, y_train, cv=5, scoring='roc_auc')
    print(f"    CV ROC-AUC Scores: {cv_scores}")
    print(f"    Mean CV ROC-AUC: {cv_scores.mean():.4f} (+/- {cv_scores.std()*2:.4f})")
    
    # Evaluation
    print("\n[5] Evaluating on test set...")
    results = classifier.evaluate(X_test, y_test)
    print(f"    Accuracy:  {results['accuracy']:.4f}")
    print(f"    Precision: {results['precision']:.4f}")
    print(f"    Recall:    {results['recall']:.4f}")
    print(f"    F1-Score:  {results['f1_score']:.4f}")
    print(f"    ROC-AUC:   {results['roc_auc']:.4f}")
    print("\n    Classification Report:")
    print(classification_report(y_test, results['predictions'], 
                               target_names=['Tidak Steril', 'Steril']))
    
    # Feature importance
    print("\n[6] Feature Importance:")
    importance = classifier.get_feature_importance()
    print(importance.to_string(index=False))
    
    # Save model
    print("\n[7] Saving model...")
    script_dir = os.path.dirname(os.path.abspath(__file__))
    model_dir = os.path.join(script_dir, 'models')
    os.makedirs(model_dir, exist_ok=True)
    
    model_path = os.path.join(model_dir, 'sterile_classifier_xgb.joblib')
    classifier.save_model(model_path)
    
    # Save plots
    print("\n[8] Saving plots...")
    output_dir = os.path.join(script_dir, 'output')
    os.makedirs(output_dir, exist_ok=True)
    
    classifier.plot_feature_importance(
        save_path=os.path.join(output_dir, 'sterile_feature_importance.png')
    )
    classifier.plot_confusion_matrix(
        y_test, results['predictions'],
        save_path=os.path.join(output_dir, 'sterile_confusion_matrix.png')
    )
    classifier.plot_roc_curve(
        y_test, results['probabilities'],
        save_path=os.path.join(output_dir, 'sterile_roc_curve.png')
    )
    
    print("\n" + "="*60)
    print("TRAINING COMPLETE!")
    print("="*60)
    
    return classifier


def demo_prediction():
    """
    Demo prediksi sterilitas dengan model yang sudah di-training.
    """
    print("\n" + "="*60)
    print("DEMO PREDIKSI STERILITAS")
    print("="*60)
    
    # Load model
    script_dir = os.path.dirname(os.path.abspath(__file__))
    model_path = os.path.join(script_dir, 'models', 'sterile_classifier_xgb.joblib')
    
    classifier = SterileClassifier()
    classifier.load_model(model_path)
    
    # Test predictions
    test_cases = [
        # Steril (air jernih)
        {'red': 250, 'green': 248, 'blue': 252, 'lux': 130, 'raw_light': 950},
        {'red': 245, 'green': 250, 'blue': 248, 'lux': 140, 'raw_light': 1000},
        
        # Tidak steril (air kuning - Rhemazol)
        {'red': 200, 'green': 180, 'blue': 50, 'lux': 35, 'raw_light': 400},
        {'red': 210, 'green': 190, 'blue': 70, 'lux': 45, 'raw_light': 500},
        
        # Tidak steril (air keruh)
        {'red': 120, 'green': 130, 'blue': 110, 'lux': 50, 'raw_light': 550},
        
        # Borderline case
        {'red': 230, 'green': 225, 'blue': 220, 'lux': 95, 'raw_light': 780},
    ]
    
    print("\nHasil Prediksi:")
    print("-"*70)
    
    for i, case in enumerate(test_cases, 1):
        result = classifier.predict_single(**case)
        status = "STERIL" if result['is_sterile'] else "TIDAK STERIL"
        
        print(f"\nTest {i}:")
        print(f"  Input: R={case['red']}, G={case['green']}, B={case['blue']}, "
              f"Lux={case['lux']}, Raw={case['raw_light']}")
        print(f"  Prediction: {status}")
        print(f"  Confidence: {result['confidence']:.2%}")
        print(f"  P(Steril): {result['probability_sterile']:.2%}")
        
        analysis = result['analysis']
        print(f"  Analysis:")
        print(f"    - Lux >= {analysis['thresholds']['lux_min']}: "
              f"{'OK' if analysis['lux_ok'] else 'FAIL'} ({case['lux']})")
        print(f"    - Raw >= {analysis['thresholds']['raw_min']}: "
              f"{'OK' if analysis['raw_ok'] else 'FAIL'} ({case['raw_light']})")
        print(f"    - Balance <= {analysis['thresholds']['balance_max']}: "
              f"{'OK' if analysis['balance_ok'] else 'FAIL'} ({analysis['rgb_balance']:.3f})")


if __name__ == "__main__":
    # Training model
    classifier = train_and_save_model()
    
    # Demo prediksi
    demo_prediction()
