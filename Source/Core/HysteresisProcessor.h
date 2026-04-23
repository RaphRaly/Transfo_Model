#pragma once
#include <cmath>
#include <algorithm>

// Forward declaration — full definition is pulled in .cpp only.
namespace transfo { class DynamicLosses; }

// =============================================================================
// HysteresisProcessor — Jiles-Atherton hysteresis model with implicit
// (Newton-Raphson) solver, following the approach of Jatin Chowdhury's
// AnalogTapeModel.
//
// This implements PHASE 1 of the Transformer Model project: the standalone
// magnetic core model. Input is interpreted as field H, output is the
// induced voltage proportional to dB/dt (Faraday's law).
//
// Solver: Newton-Raphson with trapezoidal integration (NR8 — up to 8 iters).
// Reference: Chowdhury, "Physical Modeling of Analog Audio Systems Using
//            Wave Digital Filters" (MS Thesis, Stanford CCRMA, 2020)
// =============================================================================

class HysteresisProcessor
{
public:
    HysteresisProcessor();

    void prepare(double sampleRate);
    void reset();

    // ─── Main processing function — one sample at a time ──────────────────────
    // input  : normalized audio signal [-1, 1]
    // return : audio output (proportional to dB/dt via Faraday's law)
    double process(double input);

    // ─── J-A parameters — TO BE CALIBRATED ON REAL MEASUREMENTS ───────────────
    // DO NOT invent values. Use literature ranges as placeholders.
    void setMs(double value);       // Saturation magnetization (A/m)
                                    // Literature Si-Fe: 1.0e6 – 1.7e6
    void setA(double value);        // Anhysteretic shape parameter (A/m)
                                    // Range: 10 – 200
    void setK(double value);        // Coercivity / pinning (A/m)
                                    // Range: 10 – 500
    void setC(double value);        // Reversibility coefficient
                                    // Si-Fe: 0.01 – 0.2 (NOT 0.5!)
    void setAlpha(double value);    // Inter-domain coupling
                                    // Range: 1e-5 – 1e-2

    // ─── Audio ↔ magnetic conversion ──────────────────────────────────────────
    // Manual overrides. If set, they take precedence over geometry calibration.
    void setInputScaling(double scale);    // Audio amplitude → H field (A/m)
    void setOutputScaling(double scale);   // dB/dt (T/s) → audio amplitude

    // Geometry-driven calibration (Holters/Zölzer DAFx-2016 formulation):
    //   v_phys(t) = N2 * A_c * dB/dt                      (Faraday's law)
    //   y[n]      = v_phys / V_FS_peak                    (audio normalization)
    //   outputScale = N2 * A_c / V_FS_peak
    //
    // inputScale is chosen via a static sweep so that an input of magnitude
    // A_sat produces a field H that drives B up to B_target on the virgin
    // magnetization curve. Call prepare() or recalibrate() afterwards.
    void setTransformerGeometry(double N2, double coreArea_m2, double V_FS_peak);
    void setCalibrationTarget(double B_target_T, double A_sat);
    void recalibrate();   // Recompute input/outputScale from current params+geometry

    // ─── Solver configuration ─────────────────────────────────────────────────
    void setMaxIterations(int n);          // Default: 8 (NR8)
    void setTolerance(double tol);         // Default: 1e-12

    // ─── Dynamic losses (Bertotti) coupling ───────────────────────────────────
    // Inject a non-owning DynamicLosses pointer. If null (default), the solver
    // runs quasi-statically and Bertotti coupling is bypassed completely.
    // The caller owns the DynamicLosses instance and its lifetime must exceed
    // this processor's.
    void setDynamicLosses(transfo::DynamicLosses* p) { dynLosses_ = p; }

    // UI-facing mix 0..1 that scales the injected K1/K2 coefficients linearly
    // between 0 (no dynamic losses) and the preset values stored in
    // K1_preset_ / K2_preset_. The DynamicLosses instance is updated in place.
    void setDynamicLossAmount(double mix01);

    // Preset coefficients applied when dynMix = 1.0. Defaults match the
    // Jensen JT-115K-E / Permalloy 80 Ni plate.
    void setDynamicLossPreset(double K1_preset, double K2_preset);

    // ─── Debug / monitoring ───────────────────────────────────────────────────
    double getLastMagnetization() const    { return M_prev; }
    double getLastFluxDensity() const;
    int    getLastIterationCount() const   { return lastIterCount; }
    bool   getLastConverged() const        { return lastConverged; }

private:
    // ─── J-A Parameters ───────────────────────────────────────────────────────
    // IMPORTANT: these default values are PLACEHOLDERS.
    // They MUST be replaced with calibrated values in Phase 3.
    double Ms    = 1.2e6;      // Saturation magnetization (A/m) — placeholder
    double a     = 80.0;       // Anhysteretic shape (A/m) — placeholder
    double k     = 200.0;      // Coercivity (A/m) — intentionally high for Si-Fe
    double c     = 0.1;        // Reversibility — Si-Fe typical (NOT 0.5)
    double alpha = 1e-4;       // Inter-domain coupling — MUST satisfy k > alpha*Ms
                               // Stability condition: alpha*Ms = 120 < k = 200 ✓

    // ─── Audio ↔ magnetic scaling ─────────────────────────────────────────────
    // Default values are populated by recalibrate() from the geometry below.
    // Initial values here are placeholders until prepare()/recalibrate() runs.
    double inputScale  = 100.0;   // [-1,1] audio → H in A/m
    double outputScale = 6.4e-3;  // dB/dt (T/s) → audio amplitude

    // ─── Transformer geometry (Holters reference values, Si-Fe line xfmr) ─────
    double N2_         = 1000.0;              // Secondary turns
    double coreArea_   = 4.5e-5;              // Core cross-section [m^2] (~0.45 cm^2)
    double V_FS_peak_  = 5.0 * 1.41421356237; // Full-scale peak voltage [V] (+14 dBu)

    // ─── Calibration target ───────────────────────────────────────────────────
    double B_target_   = 0.3;                 // Flux density at saturation [T]
    double A_sat_      = 1.0;                 // Input amplitude that reaches B_target

    // ─── Internal state ───────────────────────────────────────────────────────
    double M_prev    = 0.0;    // Magnetization at n-1
    double H_prev    = 0.0;    // H field at n-1
    double dMdt_prev = 0.0;    // dM/dt at n-1 (for trapezoidal rule)
    double Bprev_    = 0.0;    // B at n-1 — mirrors DynamicLosses state.
                               // Needed to compute H_eff_old = H_old - H_dyn
                               // consistently with the trapezoidal Ḃ at n-1.
    double Hdyn_prev = 0.0;    // H_dyn at n-1 (committed after solver success).
    bool   firstSample = true; // Bootstrap flag: avoid artificial dH/dt step
                               // on the very first sample after reset()

    // ─── Dynamic losses coupling (non-owning) ─────────────────────────────────
    transfo::DynamicLosses* dynLosses_ = nullptr;
    double dynMix_    = 1.0;       // 0..1, scales preset K1/K2
    double K1_preset_ = 0.02;      // JT-115K-E default, s/m
    double K2_preset_ = 0.05;      // JT-115K-E default, A·m⁻¹·(T/s)⁻⁰·⁵

    // Recompute K1/K2 on the injected DynamicLosses from mix × preset.
    void refreshDynamicCoefficients();

    // ─── Timing ───────────────────────────────────────────────────────────────
    double sampleRate_ = 44100.0;
    double Ts          = 1.0 / 44100.0;

    // ─── Solver config ────────────────────────────────────────────────────────
    // Default 20 — the Bertotti coupling adds stiffness near Ḃ=0 (turnaround)
    // so damped Newton needs more iterations than the 4–6 typical in quasi-
    // static J-A. Combined with the regularised + capped excess Jacobian
    // (DynamicLosses::kEpsJac / kExcessSlopeCap) this budget is enough for
    // nominal musical content; pathological HF stress (white noise full-band,
    // 10 kHz @ 0 dBFS below 96 kHz host rate) may still converge slower but
    // stays bounded and finite.
    int    maxIterations = 20;
    double tolerance     = 1e-12;
    int    lastIterCount = 0;
    bool   lastConverged = true;

    // ─── Internal functions ───────────────────────────────────────────────────

    // Anhysteretic magnetization: Man = Ms * L(Heff / a)
    double computeMan(double H, double M) const;

    // dM/dt as a function of (H, M, dH/dt)
    // This is f() in the trapezoidal formulation
    double computeDMdt(double H, double M, double dHdt) const;

    // Partial derivative of f with respect to M (Jacobian for NR)
    // Uses numerical approximation (central difference)
    double computeDMdt_dM(double H, double M, double dHdt) const;

    // Implicit time-step solver via Newton-Raphson
    // Returns M[n] given current and previous state
    double solveImplicit(double H_new, double H_old,
                         double M_old, double dMdt_old);

    // Enforce numerical stability invariant k > 1.5 * alpha * Ms.
    // Called from every parameter setter and from prepare().
    void enforceStability();

    // Virgin-curve sweep: find H such that B(H) on the initial magnetization
    // curve equals B_target. Returns H in A/m. Used by recalibrate().
    double findHForBTarget(double B_target) const;
};
