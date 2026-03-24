#!/usr/bin/env python3
"""
plot_thd.py — Auto-generate THD and FR validation plots (Sprint V1.5)

Reads CSVs from data/validation/, generates overlay plots with datasheet targets.
Output: PNG files in data/validation/plots/

Usage: python tools/plot_thd.py [--csv-dir data/validation] [--out-dir data/validation/plots]
"""

import os
import sys
import glob
import csv
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MPL = True
except ImportError:
    HAS_MPL = False
    print("WARNING: matplotlib not installed. Skipping plot generation.")

# Datasheet targets for overlay
DATASHEET_THD = {
    'Jensen_JT-115K-E': {
        20: [(-20, 0.065), (-2.5, 1.0), (-4, 4.0)],
    },
    'Lundahl_LL1538': {
        50: [(0, 0.2), (10, 1.0)],
    },
}

def read_thd_csv(path):
    """Read THD CSV: frequency_hz, level_dbu, thd_percent, h1_db, ..."""
    data = defaultdict(list)  # freq -> [(level, thd)]
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            freq = float(row['frequency_hz'])
            level = float(row['level_dbu'])
            thd = float(row['thd_percent'])
            data[freq].append((level, thd))
    return data

def read_fr_csv(path):
    """Read FR CSV: frequency_hz, magnitude_db"""
    freqs, mags = [], []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            freqs.append(float(row['frequency_hz']))
            mags.append(float(row['magnitude_db']))
    return freqs, mags

def plot_thd(csv_path, out_dir, preset_name):
    """Plot THD vs input level for each frequency."""
    data = read_thd_csv(csv_path)
    if not data:
        return

    fig, ax = plt.subplots(figsize=(10, 6))
    colors = plt.cm.viridis(np.linspace(0, 1, len(data)))

    for (freq, points), color in zip(sorted(data.items()), colors):
        points.sort()
        levels = [p[0] for p in points]
        thds = [max(p[1], 1e-6) for p in points]
        ax.semilogy(levels, thds, 'o-', color=color, label=f'{freq:.0f} Hz', markersize=3)

    # Overlay datasheet targets
    safe = preset_name.replace(' ', '_')
    if safe in DATASHEET_THD:
        for freq, targets in DATASHEET_THD[safe].items():
            for level, thd in targets:
                ax.plot(level, thd, 'rx', markersize=12, markeredgewidth=2)
                ax.annotate(f'{thd}%', (level, thd), textcoords="offset points",
                           xytext=(5, 5), fontsize=8, color='red')

    ax.set_xlabel('Input Level (dBu)')
    ax.set_ylabel('THD (%)')
    ax.set_title(f'THD vs Input Level — {preset_name}')
    ax.legend(fontsize=8, loc='upper left')
    ax.grid(True, alpha=0.3)

    out_path = os.path.join(out_dir, f'thd_{safe}.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f'  {out_path}')

def plot_fr(csv_path, out_dir, preset_name):
    """Plot frequency response."""
    freqs, mags = read_fr_csv(csv_path)
    if not freqs:
        return

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.semilogx(freqs, mags, 'b-', linewidth=1.5, label='Model')

    # Passband reference lines
    ax.axhline(0, color='gray', linewidth=0.5)
    ax.axhspan(-0.5, 0.5, alpha=0.1, color='green', label='±0.5 dB')

    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB re 1 kHz)')
    ax.set_title(f'Frequency Response — {preset_name}')
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(10, 22000)
    ax.set_ylim(-6, 3)

    safe = preset_name.replace(' ', '_')
    out_path = os.path.join(out_dir, f'fr_{safe}.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f'  {out_path}')

def main():
    csv_dir = 'data/validation'
    out_dir = 'data/validation/plots'

    for arg in sys.argv[1:]:
        if arg.startswith('--csv-dir='):
            csv_dir = arg.split('=')[1]
        elif arg.startswith('--out-dir='):
            out_dir = arg.split('=')[1]

    if not HAS_MPL:
        sys.exit(0)

    os.makedirs(out_dir, exist_ok=True)

    print('THD/FR Plot Generator')
    print('=====================')

    # THD plots
    thd_files = glob.glob(os.path.join(csv_dir, 'thd_*.csv'))
    for f in thd_files:
        name = os.path.basename(f).replace('thd_', '').replace('_realtime.csv', '').replace('_', ' ')
        plot_thd(f, out_dir, name)

    # FR plots
    fr_files = glob.glob(os.path.join(csv_dir, 'fr_*.csv'))
    for f in fr_files:
        name = os.path.basename(f).replace('fr_', '').replace('_realtime.csv', '').replace('_', ' ')
        plot_fr(f, out_dir, name)

    if not thd_files and not fr_files:
        print('  No CSV files found. Run tests first to generate data.')

    print('Done.')

if __name__ == '__main__':
    main()
