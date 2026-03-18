#pragma once

// =============================================================================
// BJTParams — Ebers-Moll BJT model parameters for WDF nonlinear elements.
//
// Contains static DC parameters for the Ebers-Moll large-signal model:
//   Ic = Bf * Ibe - Ibc
//   Ie = -(1+Bf)*Ibe + (1+Br)*Ibc
//   where Ibe = (Is/Bf)*(exp(Vbe/Vt)-1), Ibc = (Is/Br)*(exp(Vbc/Vt)-1)
//
// Factory presets provide datasheet-matched values for each transistor
// in the dual-topology preamp (ANALYSE_ET_DESIGN_Rev2.md):
//   Chemin A (Neve Heritage): BC184C, BC214C, BD139
//   Chemin B (JE-990 DIY):    LM394, 2N4250A, 2N2484, MJE181, MJE171
//
// Reference: Ebers-Moll 1954; SPICE BJT model Level 1;
//            Component datasheets (Philips, ON Semi, National)
// =============================================================================

#include <cmath>

namespace transfo {

struct BJTParams
{
    // ── Ebers-Moll DC parameters ────────────────────────────────────────────
    float Is  = 1e-14f;     // Saturation current [A]
    float Bf  = 200.0f;     // Forward current gain (beta_F)
    float Br  = 1.0f;       // Reverse current gain (beta_R)
    float Vt  = 0.02585f;   // Thermal voltage kT/q [V] at 25 C
    float Vaf = 100.0f;     // Forward Early voltage [V] (output impedance)

    // ── Parasitic resistances ───────────────────────────────────────────────
    float Rb  = 10.0f;      // Base spreading resistance [Ohm]
    float Rc  = 1.0f;       // Collector ohmic resistance [Ohm]
    float Re  = 0.5f;       // Emitter ohmic resistance [Ohm]

    // ── Junction capacitances (for future dynamic extension) ────────────────
    float Cje = 10e-12f;    // Base-emitter junction capacitance [F]
    float Cjc = 5e-12f;     // Base-collector junction capacitance [F]

    // ── Polarity ────────────────────────────────────────────────────────────
    enum class Polarity { NPN, PNP };
    Polarity polarity = Polarity::NPN;

    // ── Validation ──────────────────────────────────────────────────────────
    bool isValid() const
    {
        return Is > 0.0f && Is < 1e-6f
            && Bf > 1.0f && Bf < 10000.0f
            && Br > 0.0f
            && Vt > 0.020f && Vt < 0.035f
            && Vaf > 0.0f
            && Rb >= 0.0f && Rc >= 0.0f && Re >= 0.0f
            && Cje >= 0.0f && Cjc >= 0.0f;
    }

    // ── Derived quantities ──────────────────────────────────────────────────

    /// Transconductance at a given collector current [S]
    float gm(float Ic) const { return std::abs(Ic) / Vt; }

    /// Small-signal base-emitter resistance at a given Ic [Ohm]
    float rbe(float Ic) const { return Bf * Vt / (std::abs(Ic) + 1e-15f); }

    /// Small-signal output resistance (Early effect) at a given Ic [Ohm]
    float rce(float Ic) const { return Vaf / (std::abs(Ic) + 1e-15f); }

    /// Polarity sign: +1 for NPN, -1 for PNP
    float polaritySign() const { return (polarity == Polarity::NPN) ? 1.0f : -1.0f; }

    // =====================================================================
    // Factory presets — ANALYSE_ET_DESIGN_Rev2.md component values
    // =====================================================================

    // ── Chemin A: Neve Heritage (3 transistors, Classe-A) ────────────────

    /// Q1 — First common-emitter stage, NPN small signal
    /// Philips BC184C: hFE 200-800 (typ 400), low noise
    static BJTParams BC184C()
    {
        BJTParams p;
        p.Is  = 7e-15f;
        p.Bf  = 400.0f;
        p.Br  = 4.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 100.0f;
        p.Rb  = 10.0f;
        p.Rc  = 1.0f;
        p.Re  = 0.5f;
        p.Cje = 10e-12f;
        p.Cjc = 4e-12f;
        p.polarity = Polarity::NPN;
        return p;
    }

    /// Q2 — Second common-emitter stage, PNP complement of BC184C
    /// Philips BC214C: hFE 200-800 (typ 400)
    static BJTParams BC214C()
    {
        BJTParams p;
        p.Is  = 7e-15f;
        p.Bf  = 400.0f;
        p.Br  = 4.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 100.0f;
        p.Rb  = 10.0f;
        p.Rc  = 1.0f;
        p.Re  = 0.5f;
        p.Cje = 10e-12f;
        p.Cjc = 4e-12f;
        p.polarity = Polarity::PNP;
        return p;
    }

    /// Q3 — Emitter follower output stage, NPN power
    /// ST BD139: hFE 25-250 (typ 40 @ Ic=500mA), Ic_max=1.5A
    static BJTParams BD139()
    {
        BJTParams p;
        p.Is  = 1e-13f;
        p.Bf  = 40.0f;
        p.Br  = 2.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 80.0f;
        p.Rb  = 5.0f;
        p.Rc  = 0.5f;
        p.Re  = 0.3f;
        p.Cje = 30e-12f;
        p.Cjc = 30e-12f;
        p.polarity = Polarity::NPN;
        return p;
    }

    // ── Chemin B: JE-990 DIY (8 transistors, Class-AB) ──────────────────

    /// Q1/Q2 — Differential pair input, NPN matched pair
    /// National LM394: ultra-matched, en < 1 nV/sqrt(Hz)
    static BJTParams LM394()
    {
        BJTParams p;
        p.Is  = 5e-15f;
        p.Bf  = 500.0f;
        p.Br  = 2.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 200.0f;     // Very high Early voltage (precision)
        p.Rb  = 40.0f;      // Higher Rb (die-level matching structure)
        p.Rc  = 1.0f;
        p.Re  = 0.5f;
        p.Cje = 10e-12f;
        p.Cjc = 4e-12f;
        p.polarity = Polarity::NPN;
        return p;
    }

    /// Q3, Q5, Q6 — PNP cascode, current mirror, VAS
    /// ON Semi 2N4250A: hFE 100-600 (typ 250), low Cob
    static BJTParams N2N4250A()
    {
        BJTParams p;
        p.Is  = 3e-15f;
        p.Bf  = 250.0f;
        p.Br  = 4.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 50.0f;
        p.Rb  = 15.0f;
        p.Rc  = 2.0f;
        p.Re  = 1.0f;
        p.Cje = 5e-12f;
        p.Cjc = 3e-12f;
        p.polarity = Polarity::PNP;
        return p;
    }

    /// Q4 — Tail current source, Q7 — Pre-driver, NPN small signal
    /// ON Semi 2N2484: hFE 100-800 (typ 300), low noise
    static BJTParams N2N2484()
    {
        BJTParams p;
        p.Is  = 5e-15f;
        p.Bf  = 300.0f;
        p.Br  = 3.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 60.0f;
        p.Rb  = 10.0f;
        p.Rc  = 1.0f;
        p.Re  = 0.5f;
        p.Cje = 8e-12f;
        p.Cjc = 4e-12f;
        p.polarity = Polarity::NPN;
        return p;
    }

    /// Q8 — Class-AB output top (NPN)
    /// ON Semi MJE181: NPN power, hFE 20-100 (typ 30), Ic_max=3A
    static BJTParams MJE181()
    {
        BJTParams p;
        p.Is  = 5e-13f;
        p.Bf  = 30.0f;
        p.Br  = 2.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 100.0f;
        p.Rb  = 3.0f;
        p.Rc  = 0.3f;
        p.Re  = 0.2f;
        p.Cje = 50e-12f;
        p.Cjc = 30e-12f;
        p.polarity = Polarity::NPN;
        return p;
    }

    /// Q9 — Class-AB output bottom (PNP complement)
    /// ON Semi MJE171: PNP power, hFE 20-100 (typ 30), Ic_max=3A
    static BJTParams MJE171()
    {
        BJTParams p;
        p.Is  = 5e-13f;
        p.Bf  = 30.0f;
        p.Br  = 2.0f;
        p.Vt  = 0.02585f;
        p.Vaf = 100.0f;
        p.Rb  = 3.0f;
        p.Rc  = 0.3f;
        p.Re  = 0.2f;
        p.Cje = 50e-12f;
        p.Cjc = 30e-12f;
        p.polarity = Polarity::PNP;
        return p;
    }
};

} // namespace transfo
