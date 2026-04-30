#!/usr/bin/env python3
"""
plot_validation.py — Sprint 1 baseline plots from validate_jensen output.

Reads CSVs produced by validate_jensen (data/measurements/) and renders the
five plots required by Sprint 1:

  - JT115KE_thd_vs_level_20Hz.png   (THD% vs input level @ 20 Hz)
  - JT115KE_thd_vs_level_1kHz.png   (THD% vs input level @ 1 kHz)
  - JT11ELCF_thd_vs_freq.png        (THD% vs frequency at +4 dBu)
  - JT115KE_FR.png                  (Bode log-log, normalized to 1 kHz)
  - JT11ELCF_FR.png                 (Bode log-log, normalized to 1 kHz)

Each THD plot overlays the Jensen datasheet target points where available.
Each FR plot overlays the datasheet ±0.25 dB passband band.

Usage:
  python tools/plot_validation.py [--in-dir data/measurements]
                                  [--out-dir docs/measurements]
                                  [--date YYYY-MM-DD]

If --date is omitted, the latest CSVs in --in-dir are picked up automatically.
"""

import argparse
import csv
import glob
import os
import re
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
except ImportError:
    sys.stderr.write("ERROR: matplotlib is required (pip install matplotlib)\n")
    sys.exit(1)


# ── Datasheet targets (from docs/VALIDATION_REPORT.md) ─────────────────────

DATASHEET_THD_115KE = {
    # frequency_hz: [(level_dBu, thd_percent), ...]
    20.0:   [(-20.0, 0.065), (-2.5, 1.0), (1.2, 4.0)],
    1000.0: [(-20.0, 0.001), (4.0, 0.01)],
}

DATASHEET_THD_11ELCF_VS_FREQ = {
    # level_dBu @ frequency_hz: thd_percent
    (4.0, 1000.0): 0.005,
    (4.0, 50.0):   0.05,
    (4.0, 20.0):   0.028,
    (24.0, 20.0):  1.0,
}


# ── CSV readers ─────────────────────────────────────────────────────────────

def read_thd_csv(path):
    """Returns list of dicts with parsed numeric fields."""
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({
                'preset':       r['preset'],
                'mode':         r['mode'],
                'freq_hz':      float(r['freq_hz']),
                'level_dbu':    float(r['level_dbu']),
                'thd_percent':  float(r['thd_percent']),
                'gain_db':      float(r['gain_db']),
            })
    return rows


def read_fr_csv(path):
    """Returns list of (mode, freq_hz, magnitude_db) tuples."""
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append((r['mode'], float(r['frequency_hz']),
                         float(r['magnitude_db'])))
    return rows


def find_latest(in_dir, prefix, date=None):
    if date:
        path = os.path.join(in_dir, f'{prefix}_{date}.csv')
        return path if os.path.exists(path) else None
    candidates = glob.glob(os.path.join(in_dir, f'{prefix}_*.csv'))
    if not candidates:
        return None
    pat = re.compile(rf'{re.escape(prefix)}_(\d{{4}}-\d{{2}}-\d{{2}})\.csv$')
    dated = [(pat.search(os.path.basename(c)), c) for c in candidates]
    dated = [(m.group(1), c) for m, c in dated if m]
    if not dated:
        return None
    dated.sort()
    return dated[-1][1]


# ── Plot helpers ────────────────────────────────────────────────────────────

def floor_thd(values, floor=1e-4):
    """Replace zeros with a small positive floor for log scale."""
    return [max(v, floor) for v in values]


def plot_thd_vs_level(thd_rows, freq_hz, out_path, datasheet=None):
    fig, ax = plt.subplots(figsize=(8, 5))
    by_mode = defaultdict(list)
    for r in thd_rows:
        if abs(r['freq_hz'] - freq_hz) < 0.5:
            by_mode[r['mode']].append((r['level_dbu'], r['thd_percent']))

    colors = {'Realtime': 'tab:blue', 'Physical': 'tab:green'}
    for mode, points in by_mode.items():
        points.sort()
        levels = [p[0] for p in points]
        thds   = floor_thd([p[1] for p in points])
        ax.semilogy(levels, thds, 'o-', color=colors.get(mode, 'gray'),
                    markersize=6, linewidth=1.5, label=f'Model — {mode}')

    if datasheet:
        ds_levels = [p[0] for p in datasheet]
        ds_thds   = [p[1] for p in datasheet]
        ax.plot(ds_levels, ds_thds, 'r*', markersize=14,
                label='Jensen datasheet')
        for lvl, thd in datasheet:
            ax.annotate(f'{thd:g}%', (lvl, thd),
                        textcoords='offset points', xytext=(6, 6),
                        fontsize=8, color='red')

    ax.set_xlabel('Input Level (dBu)')
    ax.set_ylabel('THD (%)')
    ax.set_title(f'JT-115K-E — THD vs Input Level @ {freq_hz:.0f} Hz')
    ax.grid(True, which='both', alpha=0.3)
    ax.legend(loc='best', fontsize=9)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f'  {out_path}')


def plot_thd_vs_freq(thd_rows, level_dbu, out_path, datasheet=None):
    fig, ax = plt.subplots(figsize=(8, 5))
    by_mode = defaultdict(list)
    for r in thd_rows:
        if abs(r['level_dbu'] - level_dbu) < 0.05:
            by_mode[r['mode']].append((r['freq_hz'], r['thd_percent']))

    colors = {'Realtime': 'tab:blue', 'Physical': 'tab:green'}
    for mode, points in by_mode.items():
        points.sort()
        freqs = [p[0] for p in points]
        thds  = floor_thd([p[1] for p in points])
        ax.loglog(freqs, thds, 'o-', color=colors.get(mode, 'gray'),
                  markersize=6, linewidth=1.5, label=f'Model — {mode}')

    if datasheet:
        ds_freqs = [k[1] for k in datasheet.keys() if k[0] == level_dbu]
        ds_thds  = [v for k, v in datasheet.items() if k[0] == level_dbu]
        if ds_freqs:
            ax.plot(ds_freqs, ds_thds, 'r*', markersize=14,
                    label='Jensen datasheet')

    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('THD (%)')
    ax.set_title(f'JT-11ELCF — THD vs Frequency @ {level_dbu:+.0f} dBu')
    ax.grid(True, which='both', alpha=0.3)
    ax.legend(loc='best', fontsize=9)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f'  {out_path}')


def plot_fr(fr_rows, title, out_path):
    fig, ax = plt.subplots(figsize=(9, 5))
    by_mode = defaultdict(list)
    for mode, freq, mag in fr_rows:
        by_mode[mode].append((freq, mag))

    colors = {'Realtime': 'tab:blue', 'Physical': 'tab:green'}
    for mode, points in by_mode.items():
        points.sort()
        freqs = [p[0] for p in points]
        mags  = [p[1] for p in points]
        ax.semilogx(freqs, mags, '-', color=colors.get(mode, 'gray'),
                    linewidth=1.5, label=mode)

    ax.axhline(0, color='gray', linewidth=0.5)
    ax.axhspan(-0.25, 0.25, alpha=0.10, color='green',
               label='Datasheet ±0.25 dB')
    ax.axhline(-3.0, color='gray', linestyle='--', linewidth=0.7,
               label='-3 dB')

    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB re 1 kHz)')
    ax.set_title(title)
    ax.grid(True, which='both', alpha=0.3)
    ax.legend(loc='best', fontsize=9)
    ax.set_xlim(10, 22000)
    ax.set_ylim(-12, 4)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f'  {out_path}')


# ── Main ────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in-dir',  default='data/measurements')
    ap.add_argument('--out-dir', default='docs/measurements')
    ap.add_argument('--date',    default=None)
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    thd_115ke = find_latest(args.in_dir, 'thd_JT115KE',  args.date)
    thd_11elcf = find_latest(args.in_dir, 'thd_JT11ELCF', args.date)
    fr_115ke   = find_latest(args.in_dir, 'fr_JT115KE',   args.date)
    fr_11elcf  = find_latest(args.in_dir, 'fr_JT11ELCF',  args.date)

    print('plot_validation — Sprint 1')
    print(f'  in : {args.in_dir}')
    print(f'  out: {args.out_dir}')

    if thd_115ke:
        rows = read_thd_csv(thd_115ke)
        plot_thd_vs_level(rows, 20.0,
            os.path.join(args.out_dir, 'JT115KE_thd_vs_level_20Hz.png'),
            datasheet=DATASHEET_THD_115KE.get(20.0))
        plot_thd_vs_level(rows, 1000.0,
            os.path.join(args.out_dir, 'JT115KE_thd_vs_level_1kHz.png'),
            datasheet=DATASHEET_THD_115KE.get(1000.0))
    else:
        print(f'  MISSING: thd_JT115KE CSV in {args.in_dir}')

    if thd_11elcf:
        rows = read_thd_csv(thd_11elcf)
        plot_thd_vs_freq(rows, 4.0,
            os.path.join(args.out_dir, 'JT11ELCF_thd_vs_freq.png'),
            datasheet=DATASHEET_THD_11ELCF_VS_FREQ)
    else:
        print(f'  MISSING: thd_JT11ELCF CSV in {args.in_dir}')

    if fr_115ke:
        plot_fr(read_fr_csv(fr_115ke),
                'JT-115K-E — Frequency Response',
                os.path.join(args.out_dir, 'JT115KE_FR.png'))
    else:
        print(f'  MISSING: fr_JT115KE CSV in {args.in_dir}')

    if fr_11elcf:
        plot_fr(read_fr_csv(fr_11elcf),
                'JT-11ELCF — Frequency Response',
                os.path.join(args.out_dir, 'JT11ELCF_FR.png'))
    else:
        print(f'  MISSING: fr_JT11ELCF CSV in {args.in_dir}')


if __name__ == '__main__':
    main()
