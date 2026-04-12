#pragma once

// =============================================================================
// FluxIntegrator — Frequency-dependent saturation via Faraday's law.
//
// Problem #4 fix: Real transformers saturate more at low frequencies because
//   B_peak = V_peak / (2π·f·N·A_e)   →   flux (and saturation) scales as 1/f
//
// The existing cascade model uses a fixed hScale: H = V × hScale, which is
// frequency-independent. This produces the same saturation at 20 Hz and 20 kHz
// — incorrect physics.
//
// Solution: compander-style pre/de-emphasis around the J-A nonlinearity:
//
//   V_in → [LeakyIntegrator] → ×hScale → J-A → B → [MatchedDifferentiator] → output
//
//   Integrator:     gain ∝ f_ref / f   (boosts low freq, attenuates high freq)
//   Differentiator: gain ∝ f / f_ref   (inverse of integrator)
//
// In the LINEAR region: integrator × differentiator = 1 → flat gain.
// In SATURATION: the integrator drives more H at low frequencies, causing
// deeper saturation. The differentiator then applies dΦ/dt (Faraday's law)
// to the clipped flux → correct output waveform with frequency-dependent THD.
//
// The leaky pole at f_hp ≈ 0.5 Hz prevents DC drift in the integrator.
// The matched differentiator uses the same pole for exact cancellation.
//
// Physical justification:
//   - Integrator models Faraday's law: Φ = ∫V·dt / N
//   - J-A operates on flux (via H proportional to flux)
//   - Differentiator models output EMF: V_out = N₂·dΦ/dt
//
// Reference: Faraday's law of induction; Jiles & Atherton 1986 (flux-driven);
//            Bertotti 1998 §3.2 (frequency scaling of core losses).
// =============================================================================

#include <cmath>

namespace transfo {

class FluxIntegrator
{
public:
    FluxIntegrator() = default;

    // ─── Configuration ────────────────────────────────────────────────────
    // sampleRate : processing sample rate [Hz] (use oversampled rate if OS)
    // refFreqHz  : calibration frequency [Hz] — unity gain at this frequency
    // hpCutoffHz : leaky integrator DC-blocking pole [Hz] (default 0.5 Hz)
    void configure(double sampleRate, double refFreqHz, double hpCutoffHz = 0.5)
    {
        sampleRate_ = sampleRate;
        Ts_ = 1.0 / sampleRate;
        refFreqHz_ = refFreqHz;

        // Leaky integrator pole: z = exp(-2π·f_hp/fs)
        // At f >> f_hp, the integrator behaves as a pure integrator.
        // At f ≈ f_hp, gain rolls off to prevent DC accumulation.
        pole_ = std::exp(-2.0 * kPi * hpCutoffHz / sampleRate);

        // Normalization: at f_ref, the integrator has gain 1/(2πf_ref).
        // Multiply by 2πf_ref to normalize to unity at f_ref.
        normGain_ = 2.0 * kPi * refFreqHz;

        // Differentiator inverse normalization:
        // At f_ref, the differentiator has gain 2πf_ref·Ts (from the
        // backward difference). Divide by this to normalize to unity.
        normGainInv_ = 1.0 / (Ts_ * 2.0 * kPi * refFreqHz);

        enabled_ = (refFreqHz > 0.0 && sampleRate > 0.0);
    }

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    void reset()
    {
        intState_ = 0.0;
        diffPrev_ = 0.0;
    }

    // ─── Pre-J-A: Leaky Integration ──────────────────────────────────────
    // Converts voltage to flux proxy with 1/f frequency response.
    // At f_ref: gain = 1 (matches original hScale behavior).
    // At f < f_ref: gain = f_ref/f (more flux → more saturation).
    // At f > f_ref: gain = f_ref/f (less flux → less saturation).
    //
    // Transfer function: H_int(z) = normGain × Ts / (1 - pole·z⁻¹)
    //
    // When disabled, returns input unchanged.
    double integrate(double x)
    {
        if (!enabled_) return x;

        intState_ = pole_ * intState_ + Ts_ * x;
        return intState_ * normGain_;
    }

    // ─── Post-J-A: Matched Leaky Differentiation ─────────────────────────
    // Restores flat linear-region gain by applying f/f_ref response.
    // Exact inverse of integrate() — product is unity at all frequencies.
    //
    // Transfer function: H_diff(z) = normGainInv × (1 - pole·z⁻¹) / Ts
    //
    // Physical interpretation: V_out = N₂·dΦ/dt (Faraday's law).
    // The saturation-clipped flux waveform is differentiated to produce
    // the correct output voltage shape.
    //
    // When disabled, returns input unchanged.
    double differentiate(double x)
    {
        if (!enabled_) return x;

        const double y = (x - pole_ * diffPrev_) * normGainInv_;
        diffPrev_ = x;
        return y;
    }

    // ─── Accessors ────────────────────────────────────────────────────────
    double getRefFreqHz() const { return refFreqHz_; }
    double getPole() const { return pole_; }
    double getSampleRate() const { return sampleRate_; }

private:
    static constexpr double kPi = 3.14159265358979323846;

    double sampleRate_ = 44100.0;
    double Ts_ = 1.0 / 44100.0;
    double refFreqHz_ = 1000.0;
    double pole_ = 0.0;          // Leaky pole: exp(-2π·f_hp/fs)
    double normGain_ = 1.0;      // 2π·f_ref (integrator normalization)
    double normGainInv_ = 1.0;   // 1/(Ts·2π·f_ref) (differentiator normalization)

    // Integrator state (leaky accumulator)
    double intState_ = 0.0;

    // Differentiator state (previous input sample)
    double diffPrev_ = 0.0;

    bool enabled_ = false;
};

} // namespace transfo
