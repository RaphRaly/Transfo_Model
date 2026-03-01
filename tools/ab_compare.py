#!/usr/bin/env python3
"""
ab_compare.py — A/B comparison of two audio signals.

Reads two audio files (WAV or raw float32) and computes:
  - RMS level of each signal
  - RMS difference
  - Peak difference
  - Correlation coefficient
  - THD estimate (via FFT)
  - Spectral difference plot (optional)

Usage:
  python ab_compare.py <file_a> <file_b> [options]

Options:
  --raw               Files are raw float32 LE (not WAV)
  --samplerate <Hz>   Sample rate for raw files (default: 44100)
  --plot              Show spectral comparison plot (requires matplotlib)
  --output <file>     Save difference signal as WAV

Examples:
  python ab_compare.py ref.wav sim.wav
  python ab_compare.py ref.raw sim.raw --raw --samplerate 48000
  python ab_compare.py ref.wav sim.wav --plot
"""

import argparse
import sys
import struct
import math
import os

import numpy as np


def load_wav(path: str) -> tuple[np.ndarray, int]:
    """Load a WAV file and return (samples, samplerate)."""
    import wave
    with wave.open(path, 'rb') as wf:
        sr = wf.getframerate()
        n = wf.getnframes()
        ch = wf.getnchannels()
        sw = wf.getsampwidth()
        raw = wf.readframes(n)

    if sw == 2:
        data = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    elif sw == 3:
        # 24-bit
        samples = []
        for i in range(0, len(raw), 3):
            val = int.from_bytes(raw[i:i+3], byteorder='little', signed=True)
            samples.append(val / 8388608.0)
        data = np.array(samples, dtype=np.float32)
    elif sw == 4:
        data = np.frombuffer(raw, dtype=np.int32).astype(np.float32) / 2147483648.0
    else:
        raise ValueError(f"Unsupported sample width: {sw}")

    if ch > 1:
        data = data.reshape(-1, ch)[:, 0]  # Take first channel

    return data, sr


def load_raw(path: str) -> np.ndarray:
    """Load raw float32 LE file."""
    return np.fromfile(path, dtype=np.float32)


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x ** 2)))


def peak(x: np.ndarray) -> float:
    return float(np.max(np.abs(x)))


def correlation(a: np.ndarray, b: np.ndarray) -> float:
    """Pearson correlation coefficient."""
    if len(a) != len(b):
        n = min(len(a), len(b))
        a, b = a[:n], b[:n]
    a_mean = np.mean(a)
    b_mean = np.mean(b)
    num = np.sum((a - a_mean) * (b - b_mean))
    den = np.sqrt(np.sum((a - a_mean)**2) * np.sum((b - b_mean)**2))
    if den < 1e-15:
        return 0.0
    return float(num / den)


def thd(signal: np.ndarray, sr: int, fundamental_hz: float = 0.0) -> float:
    """
    Estimate THD from FFT.
    If fundamental_hz == 0, auto-detect from strongest bin.
    """
    N = len(signal)
    window = np.hanning(N)
    spectrum = np.abs(np.fft.rfft(signal * window))

    # Find fundamental
    if fundamental_hz <= 0:
        fund_bin = np.argmax(spectrum[1:]) + 1
    else:
        fund_bin = int(round(fundamental_hz * N / sr))

    if fund_bin < 1 or fund_bin >= len(spectrum):
        return 0.0

    fund_power = spectrum[fund_bin] ** 2

    # Sum harmonics (2nd through 10th)
    harmonic_power = 0.0
    for h in range(2, 11):
        hbin = fund_bin * h
        if hbin >= len(spectrum):
            break
        # Take max in a small window around the expected harmonic bin
        lo = max(1, hbin - 2)
        hi = min(len(spectrum) - 1, hbin + 2)
        harmonic_power += float(np.max(spectrum[lo:hi+1])) ** 2

    if fund_power < 1e-30:
        return 0.0

    return math.sqrt(harmonic_power / fund_power) * 100.0


def to_db(x: float) -> str:
    if x < 1e-15:
        return "-inf dB"
    return f"{20.0 * math.log10(x):.2f} dB"


def main():
    parser = argparse.ArgumentParser(description="A/B audio comparison")
    parser.add_argument("file_a", help="First audio file (reference)")
    parser.add_argument("file_b", help="Second audio file (simulation)")
    parser.add_argument("--raw", action="store_true", help="Files are raw float32 LE")
    parser.add_argument("--samplerate", type=int, default=44100, help="Sample rate for raw files")
    parser.add_argument("--plot", action="store_true", help="Show spectral comparison plot")
    parser.add_argument("--output", type=str, default="", help="Save difference signal as WAV")
    args = parser.parse_args()

    # Load files
    if args.raw:
        a = load_raw(args.file_a)
        b = load_raw(args.file_b)
        sr = args.samplerate
    else:
        a, sr_a = load_wav(args.file_a)
        b, sr_b = load_wav(args.file_b)
        if sr_a != sr_b:
            print(f"Warning: sample rates differ ({sr_a} vs {sr_b}), using {sr_a}")
        sr = sr_a

    # Truncate to same length
    n = min(len(a), len(b))
    a = a[:n]
    b = b[:n]

    diff = a - b

    print("=" * 60)
    print("  A/B Audio Comparison")
    print("=" * 60)
    print(f"  File A: {args.file_a}")
    print(f"  File B: {args.file_b}")
    print(f"  Samples: {n}  |  Duration: {n/sr:.3f}s  |  SR: {sr} Hz")
    print("-" * 60)
    print(f"  RMS A:          {to_db(rms(a)):>12}  ({rms(a):.6e})")
    print(f"  RMS B:          {to_db(rms(b)):>12}  ({rms(b):.6e})")
    print(f"  Peak A:         {to_db(peak(a)):>12}")
    print(f"  Peak B:         {to_db(peak(b)):>12}")
    print("-" * 60)
    print(f"  RMS Difference: {to_db(rms(diff)):>12}  ({rms(diff):.6e})")
    print(f"  Peak Difference:{to_db(peak(diff)):>12}")
    print(f"  Correlation:    {correlation(a, b):>12.8f}")
    print("-" * 60)
    print(f"  THD A:          {thd(a, sr):>11.4f}%")
    print(f"  THD B:          {thd(b, sr):>11.4f}%")
    print("=" * 60)

    # Save difference signal
    if args.output:
        import wave
        diff_int16 = np.clip(diff * 32768, -32768, 32767).astype(np.int16)
        with wave.open(args.output, 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sr)
            wf.writeframes(diff_int16.tobytes())
        print(f"  Difference saved to: {args.output}")

    # Plot
    if args.plot:
        try:
            import matplotlib.pyplot as plt

            fig, axes = plt.subplots(3, 1, figsize=(12, 8))

            # Time domain
            t = np.arange(n) / sr
            axes[0].plot(t, a, label='A (ref)', alpha=0.7)
            axes[0].plot(t, b, label='B (sim)', alpha=0.7)
            axes[0].set_xlabel('Time (s)')
            axes[0].set_ylabel('Amplitude')
            axes[0].set_title('Waveform Comparison')
            axes[0].legend()
            axes[0].grid(True, alpha=0.3)

            # Difference
            axes[1].plot(t, diff, color='red', alpha=0.7)
            axes[1].set_xlabel('Time (s)')
            axes[1].set_ylabel('A - B')
            axes[1].set_title(f'Difference (RMS: {to_db(rms(diff))})')
            axes[1].grid(True, alpha=0.3)

            # Spectrum
            N_fft = min(n, 65536)
            window = np.hanning(N_fft)
            freqs = np.fft.rfftfreq(N_fft, 1.0 / sr)

            spec_a = 20 * np.log10(np.abs(np.fft.rfft(a[:N_fft] * window)) + 1e-15)
            spec_b = 20 * np.log10(np.abs(np.fft.rfft(b[:N_fft] * window)) + 1e-15)

            axes[2].plot(freqs, spec_a, label='A (ref)', alpha=0.7)
            axes[2].plot(freqs, spec_b, label='B (sim)', alpha=0.7)
            axes[2].set_xlabel('Frequency (Hz)')
            axes[2].set_ylabel('Magnitude (dB)')
            axes[2].set_title('Spectral Comparison')
            axes[2].set_xlim(20, sr / 2)
            axes[2].set_xscale('log')
            axes[2].legend()
            axes[2].grid(True, alpha=0.3)

            plt.tight_layout()
            plt.show()

        except ImportError:
            print("  Note: install matplotlib for --plot support")

    return 0


if __name__ == "__main__":
    sys.exit(main())
