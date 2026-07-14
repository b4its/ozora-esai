"""
Color Simulation untuk Rhemazol Yellow FG
==========================================

Program ini membuat kurva line chart untuk visualisasi data sensor warna
dari cairan Rhemazol Yellow FG dengan konsentrasi 45% hingga 0% (putih).

Parameter yang divisualisasikan:
- Red (R): Nilai komponen merah (0-255)
- Green (G): Nilai komponen hijau (0-255)
- Blue (B): Nilai komponen biru (0-255)
- Lux: Intensitas cahaya (lumen per meter persegi)
- Color Temperature: Suhu warna dalam Kelvin (K)

Karakteristik Rhemazol Yellow FG:
- Pada konsentrasi tinggi (45%): Warna kuning pekat, R dan G tinggi, B rendah
- Pada konsentrasi rendah (0%): Warna putih/jernih, R, G, B seimbang tinggi
"""

import os
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec
import matplotlib.patches as mpatches


# Style configuration
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


def generate_rhemazol_data(num_points: int = 46) -> dict:
    """
    Generate simulasi data sensor warna untuk Rhemazol Yellow FG.
    
    Parameter:
    ----------
    num_points : int
        Jumlah titik data (default 46 untuk 45% hingga 0%)
    
    Returns:
    --------
    dict : Dictionary berisi konsentrasi dan nilai sensor
    
    Karakteristik simulasi:
    - Konsentrasi tinggi: Kuning pekat (R tinggi, G tinggi, B rendah)
    - Konsentrasi rendah: Putih/jernih (R, G, B seimbang tinggi)
    """
    # Konsentrasi dari 45% ke 0%
    concentration = np.linspace(45, 0, num_points)
    
    # Normalisasi konsentrasi (0-1) untuk perhitungan
    norm_conc = concentration / 45.0
    
    # === Simulasi nilai RGB ===
    # Red: Tinggi pada kuning, tetap tinggi pada putih
    red = 255 - (55 * norm_conc) + np.random.normal(0, 3, num_points)
    red = np.clip(red, 0, 255)
    
    # Green: Tinggi pada kuning, tetap tinggi pada putih
    green = 255 - (75 * norm_conc) + np.random.normal(0, 3, num_points)
    green = np.clip(green, 0, 255)
    
    # Blue: Rendah pada kuning (diserap), tinggi pada putih
    blue = 255 - (205 * norm_conc) + np.random.normal(0, 5, num_points)
    blue = np.clip(blue, 0, 255)
    
    # === Simulasi Lux ===
    # Konsentrasi tinggi: ~30 lux, konsentrasi 0: ~132 lux
    lux = 132 - (120 * norm_conc) + np.random.normal(0, 3, num_points)
    lux = np.clip(lux, 10, 132)
    
    # === Simulasi Color Temperature ===
    # Kuning: warm (~3000K), Putih: neutral (~6500K)
    color_temp = 6500 - (3500 * norm_conc) + np.random.normal(0, 100, num_points)
    color_temp = np.clip(color_temp, 2500, 7000)
    
    return {
        'concentration': concentration,
        'red': red,
        'green': green,
        'blue': blue,
        'lux': lux,
        'color_temperature': color_temp
    }


def setup_axis_style(ax, title: str, xlabel: str, ylabel: str) -> None:
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


def plot_rgb_curve(ax, data: dict) -> None:
    """Plot kurva RGB terhadap konsentrasi."""
    conc = data['concentration']
    
    ax.plot(conc, data['red'], color=COLORS['red'], linewidth=2.5, 
            label='Red', marker='o', markersize=4, markevery=5)
    ax.plot(conc, data['green'], color=COLORS['green'], linewidth=2.5, 
            label='Green', marker='s', markersize=4, markevery=5)
    ax.plot(conc, data['blue'], color=COLORS['blue'], linewidth=2.5, 
            label='Blue', marker='^', markersize=4, markevery=5)
    
    # Add shaded area for Blue to highlight absorption
    ax.fill_between(conc, data['blue'], alpha=0.1, color=COLORS['blue'])
    
    setup_axis_style(ax, 'Kurva RGB', 'Konsentrasi (%)', 'Nilai RGB (0-255)')
    ax.legend(loc='center left', fontsize=8, framealpha=0.9)
    ax.set_xlim(45, 0)
    ax.set_ylim(0, 280)


def plot_lux_curve(ax, data: dict) -> None:
    """Plot kurva Lux terhadap konsentrasi."""
    conc = data['concentration']
    
    ax.plot(conc, data['lux'], color=COLORS['lux'], linewidth=2.5, 
            label='Lux', marker='D', markersize=4, markevery=5)
    ax.fill_between(conc, data['lux'], alpha=0.2, color=COLORS['lux'])
    
    # Add min/max annotations
    ax.annotate(f"{data['lux'].min():.0f} lux", 
                xy=(45, data['lux'][0]), xytext=(40, data['lux'][0] + 15),
                fontsize=8, color=COLORS['lux'],
                arrowprops=dict(arrowstyle='->', color=COLORS['lux'], lw=0.5))
    ax.annotate(f"{data['lux'].max():.0f} lux", 
                xy=(0, data['lux'][-1]), xytext=(8, data['lux'][-1] - 15),
                fontsize=8, color=COLORS['lux'],
                arrowprops=dict(arrowstyle='->', color=COLORS['lux'], lw=0.5))
    
    setup_axis_style(ax, 'Kurva Lux (Intensitas Cahaya)', 'Konsentrasi (%)', 'Lux (lm/m²)')
    ax.legend(loc='upper left', fontsize=8, framealpha=0.9)
    ax.set_xlim(45, 0)


def plot_color_temperature_curve(ax, data: dict) -> None:
    """Plot kurva Color Temperature terhadap konsentrasi."""
    conc = data['concentration']
    
    ax.plot(conc, data['color_temperature'], color=COLORS['color_temp'], 
            linewidth=2.5, label='Color Temp', marker='p', markersize=4, markevery=5)
    ax.fill_between(conc, data['color_temperature'], alpha=0.2, color=COLORS['color_temp'])
    
    # Add temperature zone labels
    ax.axhspan(2500, 4000, alpha=0.1, color='orange', label='Warm')
    ax.axhspan(4000, 5500, alpha=0.1, color='yellow', label='Neutral')
    ax.axhspan(5500, 7000, alpha=0.1, color='lightblue', label='Cool')
    
    setup_axis_style(ax, 'Kurva Color Temperature', 'Konsentrasi (%)', 'Color Temperature (K)')
    ax.legend(loc='upper left', fontsize=7, framealpha=0.9, ncol=2)
    ax.set_xlim(45, 0)
    ax.set_ylim(2500, 7000)


def create_color_gradient_bar(ax, data: dict) -> None:
    """Buat visualisasi gradient warna dari data RGB."""
    # Buat array warna dari RGB
    colors = np.zeros((1, len(data['concentration']), 3))
    colors[0, :, 0] = data['red'] / 255
    colors[0, :, 1] = data['green'] / 255
    colors[0, :, 2] = data['blue'] / 255
    
    ax.imshow(colors, aspect='auto', extent=[45, 0, 0, 1])
    ax.set_xlabel('Konsentrasi (%)', fontsize=9, color=COLORS['text'])
    ax.set_yticks([])
    ax.set_title('Visualisasi Warna Aktual (45% ke 0%)', fontsize=10, 
                 fontweight='bold', color=COLORS['text'], pad=5)
    ax.tick_params(axis='x', labelsize=8, colors=COLORS['text'])
    
    # Add labels
    ax.text(42, 0.5, 'Kuning\nPekat', fontsize=7, ha='center', va='center', 
            color='white', fontweight='bold')
    ax.text(3, 0.5, 'Jernih atau Steril', fontsize=7, ha='center', va='center', 
            color='gray', fontweight='bold')


def create_info_panel(ax, data: dict) -> None:
    """Buat panel informasi statistik."""
    ax.axis('off')
    
    info_text = f"""
    STATISTIK DATA
    {'='*30}
    
    RGB pada 45% (Kuning Pekat):
      Red:   {data['red'][0]:.1f}
      Green: {data['green'][0]:.1f}
      Blue:  {data['blue'][0]:.1f}
    
    RGB pada 0% (Putih):
      Red:   {data['red'][-1]:.1f}
      Green: {data['green'][-1]:.1f}
      Blue:  {data['blue'][-1]:.1f}
    
    Lux Range:
      Min: {data['lux'].min():.1f} lm/m²
      Max: {data['lux'].max():.1f} lm/m²
    
    Color Temperature:
      Warm:    {data['color_temperature'][0]:.0f} K
      Neutral: {data['color_temperature'][-1]:.0f} K
    """
    
    ax.text(0.1, 0.95, info_text, transform=ax.transAxes, fontsize=8,
            verticalalignment='top', fontfamily='monospace',
            bbox=dict(boxstyle='round', facecolor='white', edgecolor=COLORS['grid'], alpha=0.9))


def print_data_table(data: dict) -> None:
    """Print tabel data dalam format yang rapi."""
    print("\n" + "="*80)
    print("DATA SENSOR WARNA - RHEMAZOL YELLOW FG")
    print("="*80)
    print(f"{'Konsentrasi':^12} | {'Red':^8} | {'Green':^8} | {'Blue':^8} | {'Lux':^10} | {'Color Temp':^12}")
    print(f"{'(%)':^12} | {'(0-255)':^8} | {'(0-255)':^8} | {'(0-255)':^8} | {'(lm/m²)':^10} | {'(K)':^12}")
    print("-"*80)
    
    # Print setiap 5 data point
    for i in range(0, len(data['concentration']), 5):
        print(f"{data['concentration'][i]:^12.1f} | {data['red'][i]:^8.1f} | "
              f"{data['green'][i]:^8.1f} | {data['blue'][i]:^8.1f} | "
              f"{data['lux'][i]:^10.1f} | {data['color_temperature'][i]:^12.1f}")
    
    print("="*80)


def main():
    """Fungsi utama untuk menjalankan simulasi dan visualisasi."""
    # Set random seed untuk reproducibility
    np.random.seed(42)
    
    # Generate data
    print("Generating data simulasi Rhemazol Yellow FG...")
    data = generate_rhemazol_data(num_points=46)
    
    # Print data table
    print_data_table(data)
    
    # Create figure dengan layout yang rapi
    fig = plt.figure(figsize=(16, 10), facecolor='white')
    
    # Perbaikan letak agar judul tidak menyatu, 'top' diubah menjadi 0.82
    gs = GridSpec(3, 4, figure=fig, height_ratios=[1, 1, 0.3], 
                  hspace=0.40, wspace=0.3, # hspace sedikit dinaikkan
                  left=0.06, right=0.94, top=0.82, bottom=0.08)
    
    # Main title dengan styling (koordinat y dipertahankan di atas area plot)
    fig.suptitle('Kurva dari Sensor Warna', 
                 fontsize=16, fontweight='bold', color=COLORS['text'], y=0.95)
    fig.text(0.5, 0.91, 'Konsentrasi 45% (Kuning Pekat) ke 0% (Jernih atau Steril)', 
             ha='center', fontsize=10, color='gray', style='italic')
    
    # Plot 1: RGB Curve (span 2 columns)
    ax1 = fig.add_subplot(gs[0, 0:2])
    plot_rgb_curve(ax1, data)
    
    # Plot 2: Color Temperature (tengah kanan)
    ax2 = fig.add_subplot(gs[0, 2])
    plot_color_temperature_curve(ax2, data)
    
    # Plot 3: Info Panel (paling kanan)
    ax_info = fig.add_subplot(gs[0, 3])
    create_info_panel(ax_info, data)
    
    # Plot 4: Lux Curve (Tengah)
    # Mengambil rentang grid 1:3 agar berada presisi di tengah layout 4 kolom
    ax3 = fig.add_subplot(gs[1, 1:3])
    plot_lux_curve(ax3, data)
    
    # Plot 5: Color Gradient Bar (span all columns)
    ax_color = fig.add_subplot(gs[2, :])
    create_color_gradient_bar(ax_color, data)
    
    # Simpan figure
    script_dir = os.path.dirname(os.path.abspath(__file__))
    output_dir = os.path.join(script_dir, 'output')
    
    # Buat folder output jika tidak ada
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Folder '{output_dir}' dibuat.")
    
    output_path = os.path.join(output_dir, 'rhemazol_yellow_fg_analysis.png')
    plt.savefig(output_path, dpi=200, bbox_inches='tight', facecolor='white', edgecolor='none')
    print(f"\nGrafik disimpan ke: {output_path}")
    
    # Tampilkan plot
    plt.show()
    
    # Return data untuk penggunaan lebih lanjut
    return data


if __name__ == "__main__":
    data = main()