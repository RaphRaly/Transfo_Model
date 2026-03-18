#pragma once

// =============================================================================
// BJTLeaf — Simplified BJT as WDF one-port nonlinear element.
//
// Models the BJT in forward-active region using the simplified Ebers-Moll:
//   - BE junction: nonlinear diode (NR-solved in wave domain)
//   - BC junction: reverse-biased → linearized (not modeled explicitly)
//   - Collector current: Ic = Bf * Ib (companion current source)
//
// The WDF port is the base-emitter junction. The collector current is an
// auxiliary output used by the enclosing WDF sub-circuit to compute the
// voltage drop across the collector load.
//
// This is analogous to JilesAthertonLeaf: a nonlinear WDOnePort solved
// via Newton-Raphson per sample, with warm-start from previous solution.
//
// For PNP transistors (BC214C, 2N4250A, MJE171), all voltages and currents
// are sign-inverted via the polarity parameter, keeping the NR equations
// universal.
//
// Operating point monitoring (getVbe, getIc, getGm) is available for
// real-time BHScope-style visualization of transistor curves.
//
// Pattern: CRTP via WDOnePort<BJTLeaf> (zero virtual overhead).
//
// Reference: Ebers-Moll 1954; chowdsp_wdf DiodeT.h (same NR pattern);
//            ANALYSE_ET_DESIGN_Rev2.md section 3.2 (Neve) & 3.3 (JE-990)
// =============================================================================

#include "../model/BJTParams.h"
#include "../util/Constants.h"
#include "BJTCompanionModel.h"
#include "WDOnePort.h"
#include <algorithm>
#include <cmath>

namespace transfo {

class BJTLeaf : public WDOnePort<BJTLeaf>
{
public:
    BJTLeaf() = default;

    // ── Configuration ────────────────────────────────────────────────────────

    void configure(const BJTParams& params)
    {
        params_ = params;
        companion_.configure(params);
        companion_.reset();

        sign_ = params.polaritySign();

        // Initial port resistance: moderate forward-bias estimate
        // rbe at Ic = 1mA: rbe = Bf * Vt / Ic = 200 * 0.026 / 0.001 = 5200 Ohm
        Z_port_ = params.rbe(1e-3f);
    }

    void prepare(float sampleRate)
    {
        sampleRate_ = sampleRate;
    }

    void reset()
    {
        companion_.reset();
        a_incident_ = 0.0f;
        b_reflected_ = 0.0f;
    }

    // ── WDOnePort CRTP interface ────────────────────────────────────────────

    /// Scatter: solve the nonlinear BE junction in the wave domain.
    ///
    /// Uses BJTCompanionModel for the NR iteration:
    ///   1. solve(a, Z) → converged Vbe
    ///   2. b = 2*Vbe - a
    ///   3. Update companion outputs (Ic, gm)
    float scatterImpl(float a)
    {
        const float Vbe = companion_.solve(a, Z_port_);
        // Update Z_port_ to track small-signal impedance at current operating point.
        // In a full WDF tree the adaptor handles this; for standalone use we self-adapt.
        Z_port_ = std::clamp(companion_.getCompanionResistance(), 1.0f, 1e8f);
        return 2.0f * Vbe - a;
    }

    /// Adaptive port resistance based on current operating point.
    ///
    /// Z = rbe = Bf * Vt / |Ic| at the current bias point.
    /// Updated periodically by the HSIM solver (every adaptationInterval).
    float getPortResistanceImpl() const
    {
        return std::clamp(companion_.getCompanionResistance(), 1.0f, 1e8f);
    }

    // ── State management (HSIM interface) ───────────────────────────────────
    void commitState()   { /* BJT is memoryless in Ebers-Moll — nothing to commit */ }
    void rollbackState() { /* Nothing to rollback */ }

    // ── Collector current output (companion source) ─────────────────────────

    /// Collector current [A] at the current operating point.
    /// This is the main output of the BJT used by the enclosing circuit:
    ///   - In CE stage: Ic flows through the collector load resistor
    ///   - In EF stage: Ic = Ie (emitter current ≈ load current)
    float getCollectorCurrent() const { return companion_.getCollectorCurrent(); }

    /// Collector voltage can be computed externally:
    /// Vc = Vcc - Ic * Rc_load (for NPN CE) or Vc = -Vcc + Ic * Rc_load (PNP CE)

    // ── Operating point monitoring ──────────────────────────────────────────

    /// Base-emitter voltage [V] (positive for NPN forward-active, neg for PNP)
    float getVbe() const { return companion_.getVbe(); }

    /// Collector current [A]
    float getIc() const { return companion_.getCollectorCurrent(); }

    /// Base current [A]
    float getIb() const { return companion_.getBaseCurrent(); }

    /// Transconductance gm = |Ic| / Vt [S]
    float getGm() const { return companion_.getTransconductance(); }

    /// Small-signal base-emitter resistance rbe [Ohm]
    float getSmallSignalRbe() const { return companion_.getCompanionResistance(); }

    /// Small-signal output resistance rce = Vaf / |Ic| [Ohm]
    float getSmallSignalRce() const { return companion_.getOutputResistance(); }

    /// NR iteration count from last scatter
    int getLastIterCount() const { return companion_.getLastIterCount(); }

    /// Access underlying parameters
    const BJTParams& getParams() const { return params_; }

private:
    BJTParams params_;
    BJTCompanionModel companion_;
    float sign_       = 1.0f;
    float sampleRate_ = 44100.0f;
};

} // namespace transfo
