#pragma once

// =============================================================================
// LCResonanceParams — Parasitic LC resonance parameters for HF modeling.
//
// Models the interaction between leakage inductance and winding/cable
// capacitance to create a second-order low-pass filter with resonance.
//
// The resonant frequency and Q factor determine the HF character:
//   f_res = 1 / (2*pi * sqrt(Lleak * C_total))
//   Q = sqrt((Rs + Rload) * Lleak * Ctotal * Rload) / (Rs * Ctotal * Rload + Lleak)
// (derived from the loaded circuit: series Rs+L, shunt C, load Rload)
//
// Filter alignment:
//   Q ~ 0.577 → Bessel (Jensen, Lundahl) — flat group delay, no overshoot
//   Q ~ 0.707 → Butterworth (API) — maximally flat magnitude
//   Q > 1.0   → Under-damped (Fender, Hammond) — resonant peak, ringing
//
// Optional Zobel network (series Rz + Cz across load) controls Q:
//   Rz = sqrt(Lleak / C_total) / target_Q
//   Cz = Lleak / Rz^2
//
// Reference: Whitlock (2005) Ch.11 Handbook for Sound Engineers;
//            nonlinear-lm-and-lc-resonance-extension.md §B.4
// =============================================================================

#include "../util/Constants.h"
#include <cmath>

namespace transfo {

struct LCResonanceParams
{
    // ── Parasitic reactances ─────────────────────────────────────────────────
    float Lleak = 5e-3f;       // Leakage inductance [H] (Jensen: 5 mH, to be fitted vs FR)
    float Cw    = 50e-12f;     // Winding capacitance (turn-to-turn + layer) [F]
    float Cp_s  = 10e-12f;     // Inter-winding capacitance (primary-secondary) [F]
    float CL    = 0.0f;        // Load / cable capacitance [F]

    // ── Zobel damping network (series RC across load) ────────────────────────
    // Set Rz = 0 to disable Zobel (natural Q, no damping).
    float Rz = 0.0f;           // Zobel resistance [Ohm] (0 = disabled)
    float Cz = 0.0f;           // Zobel capacitance [F]

    // ── Derived quantities ───────────────────────────────────────────────────

    // Total shunt capacitance -- simple model (all caps summed as shunt).
    // Retained for backward compatibility. Correct for shielded transformers
    // where Cp_s is grounded by the Faraday shield.
    float computeCtotal() const
    {
        return Cw + Cp_s + CL;
    }

    // Compute effective total capacitance accounting for Cp_s coupling.
    // turnsRatio = N2/N1 (>1 for step-up, <1 for step-down)
    // hasShield  = true if Faraday shield grounds Cp_s
    //
    // Without a shield, Cp_s is a bridging capacitance whose effective
    // contribution depends on the turns ratio (Duerdoth 1953 / Miller
    // effect). With a shield, Cp_s is simply grounded -> same as simple sum.
    float computeCtotalCorrected(float turnsRatio, bool hasShield) const
    {
        if (hasShield || turnsRatio <= 0.0f) {
            // Shield grounds Cp_s -> simple shunt model (existing behavior)
            return Cw + Cp_s + CL;
        }

        // Without shield: Duerdoth model
        float n = turnsRatio;
        float Cp_s_eff;
        if (std::abs(n - 1.0f) < 0.01f) {
            // n ~ 1: Cp_s acts as simple shunt
            Cp_s_eff = Cp_s;
        } else if (n > 1.0f) {
            // Step-up: Miller effect amplifies Cp_s on the primary side.
            // Leach (1995) practical formula: C_eff = Cp_s * (1 + 1/(n-1))
            //   = Cp_s * n / (n - 1)
            Cp_s_eff = Cp_s * (1.0f + 1.0f / (n - 1.0f));
        } else {
            // Step-down (n < 1): reduced effect -- referred capacitance scales
            Cp_s_eff = Cp_s * n;
        }

        return Cw + Cp_s_eff + CL;
    }

    // Does Cp_s bridging create a meaningful feedforward zero?
    // Returns the frequency of the zero [Hz], or 0 if shielded / negligible.
    // The zero arises from the bridging current path through Cp_s in the
    // unshielded equivalent circuit: f_zero = 1 / (2*pi * Cp_s * Rs).
    float computeCpsBridgingZeroFreq(float Rs) const
    {
        if (Cp_s <= 0.0f || Rs <= 0.0f) return 0.0f;
        return 1.0f / (kTwoPif * Cp_s * Rs);
    }

    // Resonant frequency [Hz]
    float computeFres() const
    {
        const float Ct = computeCtotal();
        if (Ct <= 0.0f || Lleak <= 0.0f)
            return 1e6f; // effectively infinite — no resonance
        return 1.0f / (kTwoPif * std::sqrt(Lleak * Ct));
    }

    // Quality factor for the actual circuit topology:
    //   Vin --[Rs]--[Lleak]--+--[Rload]-- Vout
    //                        |
    //                     [Ctotal]
    //                        |
    //                       GND
    //
    // From the 2nd-order transfer function in standard form:
    //   Q = sqrt((Rs + Rload) * L * C * Rload) / (Rs * C * Rload + L)
    //
    float computeQ(float Rs, float Rload) const
    {
        const float Ct = computeCtotal();
        if (Ct <= 0.0f || Lleak <= 0.0f || Rs <= 0.0f || Rload <= 0.0f)
            return 1.0f;
        const double Ld = Lleak, Cd = Ct, Rsd = Rs, Rld = Rload;
        const double num = std::sqrt((Rsd + Rld) * Ld * Cd * Rld);
        const double den = Rsd * Cd * Rld + Ld;
        if (den <= 0.0) return 1.0f;
        return static_cast<float>(num / den);
    }

    // DEPRECATED: Simple series RLC formula Q = sqrt(L/C) / Rs.
    // Only accurate when Rload >> Rs (load negligible). Use
    // computeQ(Rs, Rload) for the correct circuit topology Q.
    float computeQSeries(float R_total) const
    {
        const float Ct = computeCtotal();
        if (R_total <= 0.0f || Ct <= 0.0f || Lleak <= 0.0f)
            return 1.0f;
        return (1.0f / R_total) * std::sqrt(Lleak / Ct);
    }

    // Characteristic impedance of the LC network
    float computeZ0() const
    {
        const float Ct = computeCtotal();
        if (Ct <= 0.0f || Lleak <= 0.0f)
            return 1e6f;
        return std::sqrt(Lleak / Ct);
    }

    bool hasZobel() const { return Rz > 0.0f && Cz > 0.0f; }

    // ── Auto-Zobel computation ─────────────────────────────────────────────

    // Compute Zobel resistance for target Q.
    // Formula: Rz = Z0 / Q_target, where Z0 = sqrt(Lleak/Ctotal).
    // Only meaningful when natural Q > target Q (i.e., circuit needs damping).
    static float computeZobelR(float Lleak, float Ctotal, float targetQ)
    {
        if (Lleak <= 0.0f || Ctotal <= 0.0f || targetQ <= 0.0f)
            return 0.0f;
        float Z0 = std::sqrt(Lleak / Ctotal);
        return Z0 / targetQ;
    }

    // Compute Zobel capacitance from Zobel resistance.
    // Formula: Cz = Lleak / Rz^2 (matches the LC time constant).
    static float computeZobelC(float Lleak, float Rz)
    {
        if (Lleak <= 0.0f || Rz <= 0.0f)
            return 0.0f;
        return Lleak / (Rz * Rz);
    }

    // Convenience: compute both Rz and Cz for a target Q, then store them.
    void setZobelForTargetQ(float targetQ)
    {
        float Ct = computeCtotal();
        Rz = computeZobelR(Lleak, Ct, targetQ);
        Cz = computeZobelC(Lleak, Rz);
    }

    // ── Validation ───────────────────────────────────────────────────────────
    bool isPhysicallyValid() const
    {
        if (Lleak < 0.0f || Cw < 0.0f || Cp_s < 0.0f || CL < 0.0f)
            return false;
        if (Rz < 0.0f || Cz < 0.0f)
            return false;
        return true;
    }

    // ── Factory: bypass (no LC effect) ───────────────────────────────────────
    static LCResonanceParams bypass()
    {
        // Very small L, very small C → f_res >> audio band
        LCResonanceParams p;
        p.Lleak = 1e-9f;
        p.Cw = 1e-15f;
        p.Cp_s = 0.0f;
        p.CL = 0.0f;
        p.Rz = 0.0f;
        p.Cz = 0.0f;
        return p;
    }
};

} // namespace transfo
