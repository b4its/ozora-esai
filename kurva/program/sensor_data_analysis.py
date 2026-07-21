import os
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec
import openpyxl


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XLSX_PATH = os.path.join(SCRIPT_DIR, '..', 'export_tester_20260720.xlsx')
OUTPUT_DIR = os.path.join(SCRIPT_DIR, 'output')

plt.style.use('seaborn-v0_8-whitegrid')
COLORS = {
    'red': '#E74C3C',
    'green': '#27AE60',
    'blue': '#3498DB',
    'lux': '#F39C12',
    'color_temp': '#9B59B6',
    'background': '#FAFAFA',
    'grid': '#E0E0E0',
    'text': '#2C3E50'
}


def load_sensor_data(xlsx_path: str = XLSX_PATH):
    """Load sensor data from Excel file."""
    wb = openpyxl.load_workbook(xlsx_path)
    ws = wb['SensorData']
    rows = list(ws.iter_rows(min_row=2, values_only=True))

    exp2 = [r for r in rows if r[1] == 'Eksperimen dari: tester | bernama: eksperimen2 20/07']

    red = np.array([r[4] for r in exp2], dtype=float)
    green = np.array([r[5] for r in exp2], dtype=float)
    blue = np.array([r[6] for r in exp2], dtype=float)
    temp = np.array([r[7] for r in exp2], dtype=float)
    lux = np.array([r[8] for r in exp2], dtype=float)

    return red, green, blue, temp, lux


def filter_outliers(data, lower_percentile=1, upper_percentile=99):
    """Filter outliers using percentile-based clipping."""
    lower = np.percentile(data, lower_percentile)
    upper = np.percentile(data, upper_percentile)
    return np.clip(data, lower, upper)


def setup_axis_style(ax, title, xlabel, ylabel):
    """Setup consistent axis styling."""
    ax.set_title(title, fontsize=11, fontweight='bold', color=COLORS['text'], pad=10)
    ax.set_xlabel(xlabel, fontsize=9, color=COLORS['text'])
    ax.set_ylabel(ylabel, fontsize=9, color=COLORS['text'])
    ax.tick_params(axis='both', labelsize=8, colors=COLORS['text'])
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.spines['left'].set_color(COLORS['grid'])
    ax.spines['bottom'].set_color(COLORS['grid'])
    ax.grid(True, linestyle='-', alpha=0.3, color=COLORS['grid'])
    ax.set_facecolor(COLORS['background'])


def plot_rgb_curve(ax, data, time_axis):
    """Plot kurva RGB terhadap waktu."""
    red, green, blue = data['red'], data['green'], data['blue']

    ax.plot(time_axis, red, color=COLORS['red'], linewidth=1.5,
            label='Red', alpha=0.8)
    ax.plot(time_axis, green, color=COLORS['green'], linewidth=1.5,
            label='Green', alpha=0.8)
    ax.plot(time_axis, blue, color=COLORS['blue'], linewidth=1.5,
            label='Blue', alpha=0.8)

    ax.fill_between(time_axis, blue, alpha=0.1, color=COLORS['blue'])

    setup_axis_style(ax, 'Kurva RGB vs Waktu', 'Data ke-', 'Nilai RGB (0-255)')
    ax.legend(loc='upper right', fontsize=8, framealpha=0.9)
    ax.set_ylim(0, 280)


def plot_lux_curve(ax, data, time_axis):
    """Plot kurva Lux terhadap waktu."""
    lux = data['lux']

    ax.plot(time_axis, lux, color=COLORS['lux'], linewidth=1.5,
            label='Lux', alpha=0.8)
    ax.fill_between(time_axis, lux, alpha=0.2, color=COLORS['lux'])

    ax.annotate(f"{lux.min():.0f} lux",
                xy=(0, lux[0]), xytext=(len(time_axis) * 0.05, lux[0] + 10),
                fontsize=8, color=COLORS['lux'],
                arrowprops=dict(arrowstyle='->', color=COLORS['lux'], lw=0.5))
    ax.annotate(f"{lux.max():.0f} lux",
                xy=(len(time_axis) - 1, lux[-1]), xytext=(len(time_axis) * 0.7, lux[-1] + 10),
                fontsize=8, color=COLORS['lux'],
                arrowprops=dict(arrowstyle='->', color=COLORS['lux'], lw=0.5))

    setup_axis_style(ax, 'Kurva Lux (Intensitas Cahaya)', 'Data ke-', 'Lux (lm/m²)')
    ax.legend(loc='upper right', fontsize=8, framealpha=0.9)


def plot_temperature_curve(ax, data, time_axis):
    """Plot kurva Color Temperature terhadap waktu."""
    temp = data['temp']

    ax.plot(time_axis, temp, color=COLORS['color_temp'],
            linewidth=1.5, label='Color Temp', alpha=0.8)
    ax.fill_between(time_axis, temp, alpha=0.2, color=COLORS['color_temp'])

    ax.axhspan(1000, 4000, alpha=0.1, color='orange', label='Warm')
    ax.axhspan(4000, 5500, alpha=0.1, color='yellow', label='Neutral')
    ax.axhspan(5500, 8000, alpha=0.1, color='lightblue', label='Cool')

    setup_axis_style(ax, 'Kurva Color Temperature', 'Data ke-', 'Color Temperature (K)')
    ax.legend(loc='upper right', fontsize=7, framealpha=0.9, ncol=2)


def create_color_gradient_bar(ax, data):
    """Buat visualisasi gradient warna dari data RGB."""
    red, green, blue = data['red'], data['green'], data['blue']

    colors = np.zeros((1, len(red), 3))
    colors[0, :, 0] = red / 255
    colors[0, :, 1] = green / 255
    colors[0, :, 2] = blue / 255
    colors = np.clip(colors, 0, 1)

    ax.imshow(colors, aspect='auto', extent=[0, len(red), 0, 1])
    ax.set_xlabel('Data ke-', fontsize=9, color=COLORS['text'])
    ax.set_yticks([])
    ax.set_title('Visualisasi Warna Aktual (Sensor Data)', fontsize=10,
                 fontweight='bold', color=COLORS['text'], pad=5)
    ax.tick_params(axis='x', labelsize=8, colors=COLORS['text'])

    ax.text(len(red) * 0.02, 0.5, 'Awal', fontsize=7, ha='center', va='center',
            color='white', fontweight='bold')
    ax.text(len(red) * 0.95, 0.5, 'Akhir', fontsize=7, ha='center', va='center',
            color='gray', fontweight='bold')


def create_info_panel(ax, data):
    """Buat panel informasi statistik."""
    ax.axis('off')

    info_text = f"""
    STATISTIK DATA SENSOR
    {'='*30}

    RGB - Red:
      Min: {data['red'].min():.0f}
      Max: {data['red'].max():.0f}
      Rata-rata: {data['red'].mean():.1f}

    RGB - Green:
      Min: {data['green'].min():.0f}
      Max: {data['green'].max():.0f}
      Rata-rata: {data['green'].mean():.1f}

    RGB - Blue:
      Min: {data['blue'].min():.0f}
      Max: {data['blue'].max():.0f}
      Rata-rata: {data['blue'].mean():.1f}

    Color Temperature:
      Min: {data['temp'].min():.0f} K
      Max: {data['temp'].max():.0f} K
      Rata-rata: {data['temp'].mean():.0f} K

    Lux:
      Min: {data['lux'].min():.0f} lm/m²
      Max: {data['lux'].max():.0f} lm/m²
      Rata-rata: {data['lux'].mean():.1f} lm/m²

    Eksperimen: eksperimen2 20/07
    Total data: {len(data['red'])} titik
    """

    ax.text(0.1, 0.95, info_text, transform=ax.transAxes, fontsize=8,
            verticalalignment='top', fontfamily='monospace',
            bbox=dict(boxstyle='round', facecolor='white', edgecolor=COLORS['grid'], alpha=0.9))


def print_data_table(data):
    """Print tabel data dalam format yang rapi."""
    red, green, blue, temp, lux = data['red'], data['green'], data['blue'], data['temp'], data['lux']
    n = len(red)
    step = max(1, n // 20)

    print("\n" + "=" * 90)
    print("DATA SENSOR - EXPORT TESTER 2026-07-20 (eksperimen2 20/07)")
    print("=" * 90)
    print(f"{'No':^6} | {'Red':^8} | {'Green':^8} | {'Blue':^8} | {'Temp':^10} | {'Lux':^10}")
    print("-" * 90)

    for i in range(0, n, step):
        print(f"{i:^6d} | {red[i]:^8.0f} | {green[i]:^8.0f} | "
              f"{blue[i]:^8.0f} | {temp[i]:^10.0f} | {lux[i]:^10.0f}")

    print("=" * 90)


def main():
    """Fungsi utama untuk menjalankan analisis dan visualisasi."""
    if not os.path.exists(XLSX_PATH):
        print(f"Error: File tidak ditemukan: {XLSX_PATH}")
        sys.exit(1)

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print("Memuat data sensor dari export_tester_20260720.xlsx...")
    red, green, blue, temp, lux = load_sensor_data(XLSX_PATH)

    temp = filter_outliers(temp)
    lux = filter_outliers(lux)

    print(f"Total data: {len(red)} titik")
    print(f"Red: min={red.min():.0f}, max={red.max():.0f}, mean={red.mean():.1f}")
    print(f"Green: min={green.min():.0f}, max={green.max():.0f}, mean={green.mean():.1f}")
    print(f"Blue: min={blue.min():.0f}, max={blue.max():.0f}, mean={blue.mean():.1f}")
    print(f"Temp: min={temp.min():.0f}, max={temp.max():.0f}, mean={temp.mean():.0f}")
    print(f"Lux: min={lux.min():.0f}, max={lux.max():.0f}, mean={lux.mean():.1f}")

    data = {
        'red': red, 'green': green, 'blue': blue,
        'temp': temp, 'lux': lux
    }

    print_data_table(data)

    time_axis = np.arange(len(red))

    fig = plt.figure(figsize=(16, 10), facecolor='white')

    gs = GridSpec(3, 4, figure=fig, height_ratios=[1, 1, 0.3],
                  hspace=0.40, wspace=0.3,
                  left=0.06, right=0.94, top=0.82, bottom=0.08)

    fig.suptitle('Analisis Data Sensor Warna',
                 fontsize=16, fontweight='bold', color=COLORS['text'], y=0.95)
    fig.text(0.5, 0.91, 'Eksperimen: eksperimen2 20/07 | ESP32_Ozora_Portal | tester',
             ha='center', fontsize=10, color='gray', style='italic')

    ax1 = fig.add_subplot(gs[0, 0:2])
    plot_rgb_curve(ax1, data, time_axis)

    ax2 = fig.add_subplot(gs[0, 2])
    plot_temperature_curve(ax2, data, time_axis)

    ax_info = fig.add_subplot(gs[0, 3])
    create_info_panel(ax_info, data)

    ax3 = fig.add_subplot(gs[1, 1:3])
    plot_lux_curve(ax3, data, time_axis)

    ax_color = fig.add_subplot(gs[2, :])
    create_color_gradient_bar(ax_color, data)

    output_path = os.path.join(OUTPUT_DIR, 'sensor_data_analysis.png')
    plt.savefig(output_path, dpi=200, bbox_inches='tight', facecolor='white', edgecolor='none')
    print(f"\nGrafik disimpan ke: {output_path}")

    plt.show()

    return data


if __name__ == "__main__":
    data = main()
