#pragma once

// =============================================================================
// LoadIsolator — Output load isolator for the JE-990 discrete op-amp.
//
// Jensen's technique to prevent the output transformer (T2) from loading the
// feedback loop at HF. It is a simple series R+L network between the Class-AB
// output stage and the output coupling / T2 transformer.
//
// Circuit (from JE-990 schematic):
//
//     Class-AB output ──── R_load [39 Ohm] ──── L3 [40 uH] ──── to T2
//
// Behavior:
//   - At low frequencies, L3 is a short → signal passes with only the 39 Ohm
//     series resistance (negligible into typical transformer primary Z).
//   - Above f_cutoff = R / (2*pi*L) ~ 155 kHz, the inductor impedance
//     dominates → HF attenuation increases at 6 dB/oct.
//   - This isolates T2's reflected load from the op-amp's feedback network
//     at frequencies where transformer parasitics would cause instability.
//
// Implementation:
//   First-order lowpass filter (IIR) modeling the R+L voltage divider.
//   The inductor acts as a series impedance that increases with frequency,
//   rolling off the signal above f_cutoff.
//
//     alpha = 1 / (1 + 2*pi*fc*Ts)
//     y[n] = y[n-1] + alpha * (x[n] - y[n-1])
//
//   This is the simplest component in Sprint 4 — no nonlinear elements,
//   no WDF tree, no Newton-Raphson iteration.
//
// Reference: Jensen JE-990 schematic; Hardy 1979 (Operational Amplifier);
//            ANALYSE_ET_DESIGN_Rev2.md Annexe B (Dual Topology output)
// =============================================================================

#include <cmath>

namespace transfo {

// ── Configuration ────────────────────────────────────────────────────────────

struct LoadIsolatorConfig
{
    float R_series = 39.0f;    // Allen Bradley 39 Ohm [Ohm]
    float L_series = 40e-6f;   // L3 output inductor [H]

    bool isValid() const
    {
        return R_series > 0.0f
            && L_series > 0.0f;
    }
};

// ── LoadIsolator ─────────────────────────────────────────────────────────────

class LoadIsolator
{
public:
    LoadIsolator() = default;

    // ── Preparation ──────────────────────────────────────────────────────────

    /// Initialize the load isolator from config and sample rate.
    /// Computes the cutoff frequency and IIR filter coefficient.
    void prepare(float sampleRate, const LoadIsolatorConfig& config = {})
    {
        sampleRate_ = sampleRate;
        config_     = config;

        const float Ts = 1.0f / sampleRate;

        // Cutoff frequency: f_c = R / (2*pi*L)
        // For 39 Ohm / 40 uH: f_c ~ 155.2 kHz
        fc_ = config_.R_series / (2.0f * kPi * config_.L_series);

        // First-order LP coefficient:
        //   alpha = 1 / (1 + 2*pi*fc*Ts)
        //         = 1 / (1 + R*Ts/L)
        //
        // At low sample rates (e.g. 44.1 kHz), fc >> Nyquist, so alpha ~ 1
        // and the filter is essentially transparent (correct physical behavior:
        // the isolator only matters well above audio band).
        alpha_ = 1.0f / (1.0f + 2.0f * kPi * fc_ * Ts);

        reset();
    }

    /// Clear filter state to zero.
    void reset()
    {
        y1_ = 0.0f;
    }

    // ── Audio processing ─────────────────────────────────────────────────────

    /// Process a single sample through the load isolator.
    /// @param input  Voltage from the Class-AB output stage [V].
    /// @return       Filtered output voltage (HF attenuated) [V].
    float processSample(float input)
    {
        // First-order IIR lowpass:
        //   y[n] = y[n-1] + alpha * (x[n] - y[n-1])
        y1_ += alpha_ * (input - y1_);
        return y1_;
    }

    /// Block-based processing for efficiency.
    void processBlock(const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processSample(in[i]);
    }

    // ── Monitoring / diagnostics ─────────────────────────────────────────────

    /// Estimate attenuation (linear, 0..1) at a given frequency [Hz].
    ///
    /// For a first-order LP with cutoff fc:
    ///   |H(f)| = 1 / sqrt(1 + (f/fc)^2)
    ///
    /// Examples:
    ///   getAttenuation(1000)   ~ 1.0    (audio band, transparent)
    ///   getAttenuation(155000) ~ 0.707  (-3 dB at cutoff)
    ///   getAttenuation(1e6)    ~ 0.154  (-16 dB at 1 MHz)
    float getAttenuation(float freq) const
    {
        if (fc_ <= 0.0f)
            return 1.0f;
        const float ratio = freq / fc_;
        return 1.0f / std::sqrt(1.0f + ratio * ratio);
    }

    /// Series resistance contributed to the output impedance [Ohm].
    /// At DC/LF, the inductor is a short → only R_series matters.
    /// At HF, the inductor impedance adds: Z_total = R + j*2*pi*f*L.
    float getSeriesResistance() const { return config_.R_series; }

    /// Cutoff frequency of the R-L lowpass [Hz].
    float getCutoffFrequency() const { return fc_; }

    /// Access the stage configuration.
    const LoadIsolatorConfig& getConfig() const { return config_; }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    // ── Configuration ────────────────────────────────────────────────────────
    LoadIsolatorConfig config_;
    float sampleRate_ = 44100.0f;

    // ── Filter state ─────────────────────────────────────────────────────────
    float fc_    = 155000.0f;  // Cutoff frequency [Hz]
    float alpha_ = 1.0f;      // IIR coefficient
    float y1_    = 0.0f;      // Previous output sample (filter state)
};

} // namespace transfo
