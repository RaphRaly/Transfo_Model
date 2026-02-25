#pragma once
#include <cmath>
#include <algorithm>

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
    void setInputScaling(double scale);    // Audio amplitude → H field (A/m)
    void setOutputScaling(double scale);   // Magnetization M → audio amplitude

    // ─── Solver configuration ─────────────────────────────────────────────────
    void setMaxIterations(int n);          // Default: 8 (NR8)
    void setTolerance(double tol);         // Default: 1e-12

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
    double inputScale  = 1000.0;  // [-1,1] audio → H in A/m
    double outputScale = 1.0;     // M → audio amplitude

    // ─── Internal state ───────────────────────────────────────────────────────
    double M_prev    = 0.0;    // Magnetization at n-1
    double H_prev    = 0.0;    // H field at n-1
    double dMdt_prev = 0.0;    // dM/dt at n-1 (for trapezoidal rule)

    // ─── Timing ───────────────────────────────────────────────────────────────
    double sampleRate_ = 44100.0;
    double Ts          = 1.0 / 44100.0;

    // ─── Solver config ────────────────────────────────────────────────────────
    int    maxIterations = 8;
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
};
