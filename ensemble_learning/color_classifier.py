"""
Color Classifier menggunakan Random Forest
===========================================

Model ini memprediksi warna berdasarkan data sensor:
- Red (R): Nilai komponen merah (0-255 atau raw value)
- Green (G): Nilai komponen hijau
- Blue (B): Nilai komponen biru
- Lux: Intensitas cahaya
- Color Temperature: Suhu warna (K)
- Raw Light: Nilai raw dari sensor

Input dari sensor TCS34725 pada ESP32.
"""

import os
import numpy as np
import pandas as pd
import joblib
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
from sklearn.preprocessing import LabelEncoder
import matplotlib.pyplot as plt
import seaborn as sns


class ColorClassifier:
    """
    Random Forest Classifier untuk memprediksi warna dari data sensor TCS34725.
    
    Features:
    - red: Nilai komponen merah
    - green: Nilai komponen hijau
    - blue: Nilai komponen biru
    - lux: Intensitas cahaya
    - color_temp: Suhu warna (Kelvin)
    - raw_light: Nilai raw dari sensor (opsional)
    
    Labels (contoh):
    - 'kuning' (Rhemazol Yellow FG pekat)
    - 'kuning_muda' (Rhemazol Yellow FG encer)
    - 'putih' (Air jernih/steril)
    - 'merah', 'hijau', 'biru', dll (warna lain)
    """
    
    def __init__(self, n_estimators=100, random_state=42):
        """
        Inisialisasi model Random Forest.
        
        Parameters:
        -----------
        n_estimators : int
            Jumlah decision tree dalam forest
        random_state : int
            Seed untuk reproducibility
        """
        self.model = RandomForestClassifier(
            n_estimators=n_estimators,
            random_state=random_state,
            n_jobs=-1,
            max_depth=10,
            min_samples_split=5,
            min_samples_leaf=2
        )
        self.label_encoder = LabelEncoder()
        self.is_fitted = False
        self.feature_names = ['red', 'green', 'blue', 'lux', 'color_temp', 'raw_light']
        self.classes_ = None
    
    def generate_training_data(self, n_samples=1000) -> pd.DataFrame:
        """
        Generate data training simulasi untuk berbagai warna.
        
        Parameters:
        -----------
        n_samples : int
            Jumlah sampel per kelas warna
        
        Returns:
        --------
        pd.DataFrame : Dataset dengan features dan label
        """
        np.random.seed(42)
        data = []
        
        # Definisi karakteristik setiap warna
        color_profiles = {
            'kuning_pekat': {
                'red': (180, 220), 'green': (160, 200), 'blue': (30, 80),
                'lux': (20, 50), 'color_temp': (2800, 3500), 'raw_light': (300, 500)
            },
            'kuning_muda': {
                'red': (220, 250), 'green': (200, 240), 'blue': (80, 150),
                'lux': (50, 100), 'color_temp': (3500, 4500), 'raw_light': (500, 700)
            },
            'putih': {
                'red': (240, 255), 'green': (240, 255), 'blue': (240, 255),
                'lux': (100, 150), 'color_temp': (5500, 7000), 'raw_light': (800, 1000)
            },
            'merah': {
                'red': (200, 255), 'green': (20, 80), 'blue': (20, 80),
                'lux': (30, 80), 'color_temp': (2000, 3000), 'raw_light': (300, 500)
            },
            'hijau': {
                'red': (20, 80), 'green': (180, 255), 'blue': (20, 80),
                'lux': (40, 90), 'color_temp': (4000, 5500), 'raw_light': (400, 600)
            },
            'biru': {
                'red': (20, 80), 'green': (20, 100), 'blue': (180, 255),
                'lux': (30, 70), 'color_temp': (7000, 10000), 'raw_light': (350, 550)
            },
            'oranye': {
                'red': (220, 255), 'green': (100, 180), 'blue': (20, 80),
                'lux': (40, 80), 'color_temp': (2500, 3500), 'raw_light': (400, 600)
            },
            'ungu': {
                'red': (100, 180), 'green': (20, 80), 'blue': (150, 220),
                'lux': (25, 60), 'color_temp': (5000, 7000), 'raw_light': (300, 500)
            },
            'coklat': {
                'red': (120, 180), 'green': (80, 140), 'blue': (40, 100),
                'lux': (20, 50), 'color_temp': (2500, 4000), 'raw_light': (250, 450)
            },
            'abu_abu': {
                'red': (100, 180), 'green': (100, 180), 'blue': (100, 180),
                'lux': (40, 80), 'color_temp': (4500, 6000), 'raw_light': (400, 600)
            }
        }
        
        for color, profile in color_profiles.items():
            for _ in range(n_samples):
                sample = {
                    'red': np.random.uniform(*profile['red']),
                    'green': np.random.uniform(*profile['green']),
                    'blue': np.random.uniform(*profile['blue']),
                    'lux': np.random.uniform(*profile['lux']),
                    'color_temp': np.random.uniform(*profile['color_temp']),
                    'raw_light': np.random.uniform(*profile['raw_light']),
                    'color': color
                }
                # Tambahkan noise realistis
                sample['red'] += np.random.normal(0, 5)
                sample['green'] += np.random.normal(0, 5)
                sample['blue'] += np.random.normal(0, 5)
                sample['lux'] += np.random.normal(0, 3)
                
                # Clip values
                sample['red'] = np.clip(sample['red'], 0, 255)
                sample['green'] = np.clip(sample['green'], 0, 255)
                sample['blue'] = np.clip(sample['blue'], 0, 255)
                sample['lux'] = np.clip(sample['lux'], 0, 200)
                
                data.append(sample)
        
        df = pd.DataFrame(data)
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
        # Pastikan semua kolom ada
        features = []
        for col in self.feature_names:
            if col in df.columns:
                features.append(df[col].values)
            else:
                # Default value jika kolom tidak ada
                features.append(np.zeros(len(df)))
        
        X = np.column_stack(features)
        return X
    
    def fit(self, X, y):
        """
        Training model Random Forest.
        
        Parameters:
        -----------
        X : array-like
            Feature matrix (n_samples, n_features)
        y : array-like
            Label warna
        """
        # Encode labels
        y_encoded = self.label_encoder.fit_transform(y)
        self.classes_ = self.label_encoder.classes_
        
        # Fit model
        self.model.fit(X, y_encoded)
        self.is_fitted = True
        
        print(f"Model trained with {len(self.classes_)} classes: {list(self.classes_)}")
        return self
    
    def predict(self, X) -> np.ndarray:
        """
        Prediksi warna dari data sensor.
        
        Parameters:
        -----------
        X : array-like
            Feature matrix
        
        Returns:
        --------
        np.ndarray : Predicted color labels
        """
        if not self.is_fitted:
            raise ValueError("Model belum di-training. Panggil fit() terlebih dahulu.")
        
        y_pred_encoded = self.model.predict(X)
        y_pred = self.label_encoder.inverse_transform(y_pred_encoded)
        return y_pred
    
    def predict_proba(self, X) -> dict:
        """
        Prediksi probabilitas untuk setiap kelas warna.
        
        Parameters:
        -----------
        X : array-like
            Feature matrix
        
        Returns:
        --------
        dict : Dictionary dengan probabilitas per kelas
        """
        if not self.is_fitted:
            raise ValueError("Model belum di-training.")
        
        proba = self.model.predict_proba(X)
        results = []
        for p in proba:
            result = {cls: prob for cls, prob in zip(self.classes_, p)}
            results.append(result)
        return results
    
    def predict_single(self, red, green, blue, lux, color_temp=0, raw_light=0) -> dict:
        """
        Prediksi warna dari single sensor reading.
        
        Parameters:
        -----------
        red, green, blue : float
            Nilai RGB
        lux : float
            Intensitas cahaya
        color_temp : float
            Suhu warna (Kelvin)
        raw_light : float
            Nilai raw sensor
        
        Returns:
        --------
        dict : Hasil prediksi dengan probabilitas
        """
        X = np.array([[red, green, blue, lux, color_temp, raw_light]])
        prediction = self.predict(X)[0]
        probabilities = self.predict_proba(X)[0]
        
        return {
            'predicted_color': prediction,
            'confidence': probabilities[prediction],
            'probabilities': probabilities
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
        accuracy = accuracy_score(y_test, y_pred)
        report = classification_report(y_test, y_pred, output_dict=True)
        cm = confusion_matrix(y_test, y_pred)
        
        return {
            'accuracy': accuracy,
            'classification_report': report,
            'confusion_matrix': cm,
            'predictions': y_pred
        }
    
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
        bars = ax.barh(importance['feature'], importance['importance'], color='steelblue')
        ax.set_xlabel('Importance')
        ax.set_title('Feature Importance - Random Forest Color Classifier')
        ax.invert_yaxis()
        
        # Add value labels
        for bar, val in zip(bars, importance['importance']):
            ax.text(val + 0.01, bar.get_y() + bar.get_height()/2, 
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
        
        fig, ax = plt.subplots(figsize=(12, 10))
        sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                   xticklabels=self.classes_, yticklabels=self.classes_, ax=ax)
        ax.set_xlabel('Predicted')
        ax.set_ylabel('Actual')
        ax.set_title('Confusion Matrix - Color Classifier')
        
        plt.tight_layout()
        
        if save_path:
            plt.savefig(save_path, dpi=150, bbox_inches='tight')
            print(f"Confusion matrix saved to: {save_path}")
        
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
            'label_encoder': self.label_encoder,
            'feature_names': self.feature_names,
            'classes': self.classes_,
            'is_fitted': self.is_fitted
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
        self.label_encoder = model_data['label_encoder']
        self.feature_names = model_data['feature_names']
        self.classes_ = model_data['classes']
        self.is_fitted = model_data['is_fitted']
        print(f"Model loaded from: {filepath}")
        print(f"Classes: {list(self.classes_)}")


def train_and_save_model():
    """
    Training model dan simpan ke file.
    """
    print("="*60)
    print("TRAINING COLOR CLASSIFIER (Random Forest)")
    print("="*60)
    
    # Inisialisasi classifier
    classifier = ColorClassifier(n_estimators=100, random_state=42)
    
    # Generate training data
    print("\n[1] Generating training data...")
    df = classifier.generate_training_data(n_samples=500)
    print(f"    Total samples: {len(df)}")
    print(f"    Classes: {df['color'].unique()}")
    
    # Prepare features dan labels
    X = classifier.prepare_features(df)
    y = df['color'].values
    
    # Split data
    print("\n[2] Splitting data (80% train, 20% test)...")
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )
    print(f"    Train samples: {len(X_train)}")
    print(f"    Test samples: {len(X_test)}")
    
    # Training
    print("\n[3] Training Random Forest model...")
    classifier.fit(X_train, y_train)
    
    # Cross-validation
    print("\n[4] Cross-validation (5-fold)...")
    y_train_encoded = classifier.label_encoder.transform(y_train)
    cv_scores = cross_val_score(classifier.model, X_train, y_train_encoded, cv=5)
    print(f"    CV Scores: {cv_scores}")
    print(f"    Mean CV Score: {cv_scores.mean():.4f} (+/- {cv_scores.std()*2:.4f})")
    
    # Evaluation
    print("\n[5] Evaluating on test set...")
    results = classifier.evaluate(X_test, y_test)
    print(f"    Accuracy: {results['accuracy']:.4f}")
    print("\n    Classification Report:")
    print(classification_report(y_test, results['predictions']))
    
    # Feature importance
    print("\n[6] Feature Importance:")
    importance = classifier.get_feature_importance()
    print(importance.to_string(index=False))
    
    # Save model
    print("\n[7] Saving model...")
    script_dir = os.path.dirname(os.path.abspath(__file__))
    model_dir = os.path.join(script_dir, 'models')
    os.makedirs(model_dir, exist_ok=True)
    
    model_path = os.path.join(model_dir, 'color_classifier_rf.joblib')
    classifier.save_model(model_path)
    
    # Save plots
    print("\n[8] Saving plots...")
    output_dir = os.path.join(script_dir, 'output')
    os.makedirs(output_dir, exist_ok=True)
    
    classifier.plot_feature_importance(
        save_path=os.path.join(output_dir, 'color_feature_importance.png')
    )
    classifier.plot_confusion_matrix(
        y_test, results['predictions'],
        save_path=os.path.join(output_dir, 'color_confusion_matrix.png')
    )
    
    print("\n" + "="*60)
    print("TRAINING COMPLETE!")
    print("="*60)
    
    return classifier


def demo_prediction():
    """
    Demo prediksi warna dengan model yang sudah di-training.
    """
    print("\n" + "="*60)
    print("DEMO PREDIKSI WARNA")
    print("="*60)
    
    # Load model
    script_dir = os.path.dirname(os.path.abspath(__file__))
    model_path = os.path.join(script_dir, 'models', 'color_classifier_rf.joblib')
    
    classifier = ColorClassifier()
    classifier.load_model(model_path)
    
    # Test predictions
    test_cases = [
        {'red': 200, 'green': 180, 'blue': 50, 'lux': 35, 'color_temp': 3000, 'raw_light': 400},
        {'red': 250, 'green': 250, 'blue': 250, 'lux': 130, 'color_temp': 6500, 'raw_light': 900},
        {'red': 230, 'green': 70, 'blue': 50, 'lux': 50, 'color_temp': 2500, 'raw_light': 400},
        {'red': 50, 'green': 200, 'blue': 60, 'lux': 60, 'color_temp': 5000, 'raw_light': 500},
        {'red': 60, 'green': 80, 'blue': 220, 'lux': 45, 'color_temp': 8000, 'raw_light': 450},
    ]
    
    print("\nHasil Prediksi:")
    print("-"*60)
    
    for i, case in enumerate(test_cases, 1):
        result = classifier.predict_single(**case)
        print(f"\nTest {i}:")
        print(f"  Input: R={case['red']}, G={case['green']}, B={case['blue']}, Lux={case['lux']}")
        print(f"  Predicted Color: {result['predicted_color']}")
        print(f"  Confidence: {result['confidence']:.2%}")
        print(f"  Top 3 Probabilities:")
        sorted_probs = sorted(result['probabilities'].items(), key=lambda x: x[1], reverse=True)[:3]
        for color, prob in sorted_probs:
            print(f"    - {color}: {prob:.2%}")


if __name__ == "__main__":
    # Training model
    classifier = train_and_save_model()
    
    # Demo prediksi
    demo_prediction()
