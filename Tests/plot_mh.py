"""Plot M-H hysteresis loops from CSV test data."""
import csv
import os

# Try matplotlib first, fall back to text-based analysis
try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend
    import matplotlib.pyplot as plt
    HAS_PLT = True
except ImportError:
    HAS_PLT = False
    print("matplotlib not available — doing numerical analysis only.\n")

os.chdir(os.path.dirname(os.path.abspath(__file__)))

def read_csv(filename):
    H, M = [], []
    with open(filename) as f:
        reader = csv.DictReader(f)
        for row in reader:
            H.append(float(row['H']))
            M.append(float(row['M']))
    return H, M

# ── Test 1: Single M-H loop ──
print("=== Test 1: M-H Loop Shape Analysis ===")
H, M = read_csv('test1_MH_loop_1Hz.csv')
print(f"  Points: {len(H)}")
print(f"  H range: [{min(H):.1f}, {max(H):.1f}] A/m")
print(f"  M range: [{min(M):.1f}, {max(M):.1f}] A/m")
print(f"  M crosses zero: {any(a*b < 0 for a, b in zip(M[:-1], M[1:]))}")

# Check S-shape: when H=0, M should have two distinct values (upper/lower branch)
near_zero_H = [(h, m) for h, m in zip(H, M) if abs(h) < 5.0]
if near_zero_H:
    m_at_zero = [m for _, m in near_zero_H]
    m_min_zero = min(m_at_zero)
    m_max_zero = max(m_at_zero)
    print(f"  M at H~0: [{m_min_zero:.1f}, {m_max_zero:.1f}] -- gap = {m_max_zero - m_min_zero:.1f}")
    if m_max_zero - m_min_zero > 1000:
        print("  [OK] Two distinct branches at H=0 -> S-shape confirmed (remanent magnetization)")
    else:
        print("  [FAIL] No branch separation at H=0 -> NOT a proper hysteresis loop!")

if HAS_PLT:
    # Plot 1: Single loop
    fig, ax = plt.subplots(1, 1, figsize=(8, 6))
    # Subsample for plotting (768k points is too many)
    step = max(1, len(H) // 5000)
    ax.plot([H[i] for i in range(0, len(H), step)],
            [M[i] for i in range(0, len(M), step)],
            linewidth=0.5, color='blue')
    ax.set_xlabel('H (A/m)')
    ax.set_ylabel('M (A/m)')
    ax.set_title('Test 1: M-H Hysteresis Loop (1 Hz, amplitude=500 A/m)')
    ax.grid(True, alpha=0.3)
    ax.axhline(y=0, color='k', linewidth=0.5)
    ax.axvline(x=0, color='k', linewidth=0.5)
    fig.tight_layout()
    fig.savefig('test1_MH_loop.png', dpi=150)
    print("  -> Saved test1_MH_loop.png")

    # Plot 2: Multiple amplitudes (saturation test)
    fig2, ax2 = plt.subplots(1, 1, figsize=(8, 6))
    amps = [100, 300, 500, 1000, 2000]
    colors = ['green', 'blue', 'orange', 'red', 'purple']
    for amp, color in zip(amps, colors):
        fname = f'test2_MH_amp_{amp}.csv'
        if os.path.exists(fname):
            h, m = read_csv(fname)
            step = max(1, len(h) // 3000)
            ax2.plot([h[i] for i in range(0, len(h), step)],
                     [m[i] for i in range(0, len(m), step)],
                     linewidth=0.5, color=color, label=f'H_amp={amp} A/m')
    ax2.set_xlabel('H (A/m)')
    ax2.set_ylabel('M (A/m)')
    ax2.set_title('Test 2: Saturation — M-H loops at increasing amplitudes')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.axhline(y=0, color='k', linewidth=0.5)
    ax2.axvline(x=0, color='k', linewidth=0.5)
    fig2.tight_layout()
    fig2.savefig('test2_saturation.png', dpi=150)
    print("  -> Saved test2_saturation.png")

print("\nDone.")
