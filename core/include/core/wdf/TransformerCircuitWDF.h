#pragma once

// =============================================================================
// TransformerCircuitWDF — Unified WDF tree for the JT-115K-E transformer
//                         test circuit (T-model, all values referred to primary).
//
// Replaces the cascade HP -> J-A -> LC pipeline in TransformerModel with a
// single physically-correct Wave Digital Filter network.
//
// T-model circuit (referred to primary):
//
//       Rs=150        Rdc_pri=19.7    Lleak         node A       Rdc_sec/n^2    Rload/n^2
// Vin --[Rs]----------[Rdc_pri]------[Lleak]----------+----------[Rdc_sec_ref]--[Rload_ref]-- GND
//                                                     |                              |
//                                                  [Lm(t)]                    [C_sec*n^2=20.5nF]
//                                                 (non-lin)                          |
//                                                     |                             GND
//                                                    GND
//                                                     |
//                                               [C_pri=475pF]
//                                                     |
//                                                    GND
//
// WDF tree decomposition:
//
//                        Lm(t)  <-- ROOT LEAF (nonlinear, J-A or CPWL)
//                          |
//               P_root: DynamicParallelAdaptor<4>
//              (adapted port -> Lm, 3 child ports)
//             /            |                \
//        S_source      C_pri_shield       S_load
//       (Series^2)     (Capacitor)       (Series of Rdc_sec_ref + P_load)
//      /    |    \       475 pF          /       \
//  RSource Rdc  Lleak             Rdc_sec_ref   P_load: Parallel<2>
//  (Vs,Rs) 19.7  (ind)            24.65         /          \
//  150                                      Rload_ref   C_sec_ref
//                                            1500        20.5nF
//
// Template parameter NonlinearLeaf is JilesAthertonLeaf<LangevinPade>
// (Physical mode) or CPWLLeaf (Realtime mode).
//
// Reference: Fettweis 1986; chowdsp_wdf; Werner thesis; Polimi thesis
// =============================================================================

#include "WDOnePort.h"
#include "AdaptedElements.h"
#include "WDFSeriesAdaptor.h"
#include "DynamicParallelAdaptor.h"
#include "../magnetics/JilesAthertonLeaf.h"
#include "../magnetics/CPWLLeaf.h"
#include "../model/TransformerConfig.h"
#include "../util/Constants.h"

#include <type_traits>

namespace transfo {

template <typename NonlinearLeaf>
class TransformerCircuitWDF
{
public:
    TransformerCircuitWDF() = default;

    // ─── Preparation ─────────────────────────────────────────────────────────

    /// Initialize all elements and adaptors from config at the given sample rate.
    /// Must be called before processSample().
    void prepare(double sampleRate, const TransformerConfig& cfg)
    {
        sampleRate_ = static_cast<float>(sampleRate);
        turnsRatio_ = cfg.windings.turnsRatio();

        const float Ts = 1.0f / sampleRate_;
        const float n  = turnsRatio_;

        // Reflected secondary values (referred to primary)
        const float Rdc_sec_ref = cfg.Rdc_sec_reflected();
        const float Rload_ref   = cfg.Rload_reflected();
        const float C_sec_ref   = cfg.C_sec_reflected();

        // ── Configure leaves ────────────────────────────────────────────────

        source_.setSourceImpedance(cfg.windings.sourceImpedance);
        source_.setVoltage(0.0f);

        rdcPri_.setResistance(cfg.windings.Rdc_primary);

        lleak_.setInductance(cfg.windings.L_leakage, Ts);

        cPriShield_.setCapacitance(cfg.windings.C_pri_shield, Ts);

        rdcSecRef_.setResistance(Rdc_sec_ref);

        rloadRef_.setResistance(Rload_ref);

        cSecRef_.setCapacitance(C_sec_ref, Ts);

        // Configure nonlinear leaf (type-dependent)
        configureNonlinearLeaf(cfg, sampleRate);

        // ── Wire up adaptors ────────────────────────────────────────────────

        // Source branch: nested series
        //   sSourceInner_ = Rs + Rdc_pri
        //   sSourceOuter_ = sSourceInner + Lleak
        sSourceInner_.setPortImpedances(source_.getPortResistance(),
                                        rdcPri_.getPortResistance());
        sSourceOuter_.setPortImpedances(sSourceInner_.getAdaptedImpedance(),
                                        lleak_.getPortResistance());

        // Load branch: parallel Rload||Csec, then series with Rdc_sec
        pLoad_.setPortImpedance(0, rloadRef_.getPortResistance());
        pLoad_.setPortImpedance(1, cSecRef_.getPortResistance());
        pLoad_.setAdaptedPort(1);  // port 1 faces upward
        pLoad_.recalculateScattering();

        sLoad_.setPortImpedances(rdcSecRef_.getPortResistance(),
                                 pLoad_.getAdaptedImpedance());

        // Root parallel junction: 4 ports
        //   port 0 = source branch (sSourceOuter)
        //   port 1 = C_pri_shield
        //   port 2 = load branch (sLoad)
        //   port 3 = Lm (nonlinear, adapted)
        pRoot_.setPortImpedance(0, sSourceOuter_.getAdaptedImpedance());
        pRoot_.setPortImpedance(1, cPriShield_.getPortResistance());
        pRoot_.setPortImpedance(2, sLoad_.getAdaptedImpedance());
        pRoot_.setPortImpedance(3, lm_.getPortResistance());
        pRoot_.setAdaptedPort(3);
        pRoot_.recalculateScattering();

        // Reset adaptation counter
        adaptCounter_ = 0;
    }

    // ─── Sample processing (hot path) ────────────────────────────────────────

    /// Process one sample through the full WDF tree.
    /// @param Vin  Input voltage at the primary (after source impedance)
    /// @return     Output voltage at the secondary (scaled by turns ratio)
    float processSample(float Vin)
    {
        source_.setVoltage(Vin);

        // ═══════════════════════════════════════════════════════════════════
        // UP-SWEEP: collect reflected waves (b) from leaves, propagate up
        // ═══════════════════════════════════════════════════════════════════
        //
        // For adapted leaves the reflected wave b does NOT depend on the
        // incident wave a, so we can read b without knowing a yet.
        //
        // AdaptedRSource:  b = Vg  (stateless, but Vg may have changed)
        // AdaptedResistor: b = 0
        // AdaptedCapacitor: b = state_ (from previous scatter)
        // AdaptedInductor:  b = -state_ (from previous scatter)
        //
        // We re-scatter the source to capture the new Vin.  For the
        // resistor-type source, scatter(a) = Vg regardless of a, so
        // passing the stored incident is safe (no state corruption).

        const float b_src   = source_.scatter(source_.getIncidentWave());
        const float b_rdc   = 0.0f;  // Resistor: always 0
        const float b_lleak = lleak_.getReflectedWaveFromState();
        const float b_cPri  = cPriShield_.getReflectedWaveFromState();
        const float b_rdcSec = 0.0f; // Resistor: always 0
        const float b_rload  = 0.0f; // Resistor: always 0
        const float b_cSec   = cSecRef_.getReflectedWaveFromState();

        // ── Source series adaptors (up) ─────────────────────────────────
        // For an adapted series junction: b_parent = a_child1 + a_child2
        const float b_sSourceInner = b_src + b_rdc;
        const float b_sSourceOuter = b_sSourceInner + b_lleak;

        // ── Load parallel adaptor (up) ──────────────────────────────────
        const float a_pLoad_up[2] = { b_rload, b_cSec };
        const float b_pLoad_adapted = pLoad_.scatterAdapted(a_pLoad_up);

        // ── Load series adaptor (up) ────────────────────────────────────
        const float b_sLoad = b_rdcSec + b_pLoad_adapted;

        // ═══════════════════════════════════════════════════════════════════
        // ROOT: parallel junction computes incident wave to Lm (adapted)
        // ═══════════════════════════════════════════════════════════════════

        float a_root[4];
        a_root[0] = b_sSourceOuter;
        a_root[1] = b_cPri;
        a_root[2] = b_sLoad;
        a_root[3] = 0.0f;  // placeholder for adapted port
        const float a_lm = pRoot_.scatterAdapted(a_root);

        // ═══════════════════════════════════════════════════════════════════
        // NONLINEAR SOLVE: Lm scattering (Newton-Raphson inside)
        // ═══════════════════════════════════════════════════════════════════

        const float b_lm = lm_.scatter(a_lm);

        // ═══════════════════════════════════════════════════════════════════
        // DOWN-SWEEP: root distributes reflected waves back to all leaves
        // ═══════════════════════════════════════════════════════════════════

        a_root[3] = b_lm;
        float b_root[4];
        pRoot_.scatter(a_root, b_root);
        // b_root[0] -> source branch, b_root[1] -> C_pri, b_root[2] -> load branch

        // ── Source outer series (down) ──────────────────────────────────
        float b_sOuter_1, b_sOuter_2;
        sSourceOuter_.scatterDown(b_sSourceInner, b_lleak, b_root[0],
                                  b_sOuter_1, b_sOuter_2);

        // ── Source inner series (down) ──────────────────────────────────
        float b_sInner_1, b_sInner_2;
        sSourceInner_.scatterDown(b_src, b_rdc, b_sOuter_1,
                                  b_sInner_1, b_sInner_2);

        // ── Load series (down) ──────────────────────────────────────────
        float b_sLoad_1, b_sLoad_2;
        sLoad_.scatterDown(b_rdcSec, b_pLoad_adapted, b_root[2],
                           b_sLoad_1, b_sLoad_2);

        // ── Load parallel (down) ────────────────────────────────────────
        // Replace the adapted port's wave with the parent's incident and
        // re-scatter to obtain incident waves for child leaves.
        float a_pLoad_final[2] = { b_rload, b_cSec };
        a_pLoad_final[pLoad_.getAdaptedPort()] = b_sLoad_2;
        float b_pLoad_final[2];
        pLoad_.scatter(a_pLoad_final, b_pLoad_final);

        // ═══════════════════════════════════════════════════════════════════
        // UPDATE LEAVES: scatter with final incident waves (stores state)
        // ═══════════════════════════════════════════════════════════════════
        //
        // For reactive elements (capacitor, inductor) calling scatter(a)
        // returns the current b and sets state_ = a for the NEXT sample.
        // For resistors / source the call is harmless (stateless).

        source_.scatter(b_sInner_1);
        rdcPri_.scatter(b_sInner_2);
        lleak_.scatter(b_sOuter_2);
        cPriShield_.scatter(b_root[1]);
        rdcSecRef_.scatter(b_sLoad_1);
        rloadRef_.scatter(b_pLoad_final[0]);
        cSecRef_.scatter(b_pLoad_final[1]);

        // Commit nonlinear leaf state (J-A: commit M; CPWL: no-op)
        lm_.commitState();

        // ── Periodic adaptation of nonlinear port impedance ─────────────
        if (++adaptCounter_ >= kDefaultAdaptationInterval) {
            adaptCounter_ = 0;
            updateAdaptation();
        }

        // ═══════════════════════════════════════════════════════════════════
        // OUTPUT: secondary voltage = n * V_Rload (referred to primary)
        // ═══════════════════════════════════════════════════════════════════
        //
        // After the final scatter, rloadRef_ stores:
        //   a_incident  = b_pLoad_final[0]
        //   b_reflected = 0  (adapted resistor)
        // Kirchhoff voltage: V = (a + b) / 2 = a / 2
        //
        // The turns ratio transforms back to secondary voltage.

        return turnsRatio_ * rloadRef_.getKirchhoffV();
    }

    // ─── Reset ───────────────────────────────────────────────────────────────

    /// Reset all reactive element states and the nonlinear leaf.
    void reset()
    {
        lleak_.reset();
        cPriShield_.reset();
        cSecRef_.reset();
        lm_.reset();
        adaptCounter_ = 0;

        // Zero stored wave variables on all leaves
        source_.setIncidentWave(0.0f);
        rdcPri_.setIncidentWave(0.0f);
        lleak_.setIncidentWave(0.0f);
        cPriShield_.setIncidentWave(0.0f);
        rdcSecRef_.setIncidentWave(0.0f);
        rloadRef_.setIncidentWave(0.0f);
        cSecRef_.setIncidentWave(0.0f);

        // Force an initial scatter so getReflectedWave() returns valid values
        source_.scatter(0.0f);
        rdcPri_.scatter(0.0f);
        lleak_.scatter(0.0f);
        cPriShield_.scatter(0.0f);
        rdcSecRef_.scatter(0.0f);
        rloadRef_.scatter(0.0f);
        cSecRef_.scatter(0.0f);
    }

    // ─── Impedance adaptation ────────────────────────────────────────────────

    /// Update the nonlinear leaf's port resistance in the root junction.
    /// Called periodically (every kDefaultAdaptationInterval samples).
    void updateAdaptation()
    {
        const float newZ = lm_.getPortResistance();
        pRoot_.setPortImpedance(3, newZ);
        pRoot_.recalculateScattering();
    }

    // ─── Monitoring / diagnostics ────────────────────────────────────────────

    /// Input impedance at the primary terminals: V_source / I_source.
    float getInputImpedance() const
    {
        const float V = source_.getKirchhoffV();
        const float I = source_.getKirchhoffI();
        if (std::abs(I) < kEpsilonF)
            return 0.0f;
        return V / I;
    }

    /// Instantaneous magnetizing inductance from the nonlinear leaf.
    float getLm() const
    {
        // Lm = Z_port * Ts / 2  (from trapezoidal discretization Z = 2L/Ts)
        const float Z = lm_.getPortResistance();
        const float Ts = 1.0f / sampleRate_;
        return Z * Ts * 0.5f;
    }

    /// Access the nonlinear leaf for parameter updates and B-H monitoring.
    NonlinearLeaf&       getNonlinearLeaf()       { return lm_; }
    const NonlinearLeaf& getNonlinearLeaf() const { return lm_; }

    /// Current turns ratio.
    float getTurnsRatio() const { return turnsRatio_; }

    /// Current sample rate.
    float getSampleRate() const { return sampleRate_; }

private:
    // ─── Nonlinear leaf configuration (type-dispatched) ──────────────────────

    void configureNonlinearLeaf(const TransformerConfig& cfg, double sampleRate)
    {
        if constexpr (std::is_same_v<NonlinearLeaf, CPWLLeaf>) {
            // CPWLLeaf uses setGeometry() — segment data set externally via
            // setAscendingSegments / setDescendingSegments / precomputeADAACoeffs
            lm_.setGeometry(cfg.core.effectiveLength(),
                            cfg.core.effectiveArea());
            lm_.reset();
        }
        else {
            // JilesAthertonLeaf: full configure with geometry + material + rate
            lm_.configure(cfg.core.effectiveLength(),
                          cfg.core.effectiveArea(),
                          cfg.material,
                          sampleRate);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // LEAVES
    // ═══════════════════════════════════════════════════════════════════════════

    AdaptedRSource    source_{150.0f, 0.0f};  ///< Vs + Rs (150 Ohm)
    AdaptedResistor   rdcPri_{19.7f};         ///< Primary DC resistance
    AdaptedInductor   lleak_{5e-3f};          ///< Leakage inductance
    AdaptedCapacitor  cPriShield_{475e-12f};  ///< Primary-to-shield capacitance
    AdaptedResistor   rdcSecRef_{24.65f};     ///< Rdc_sec reflected to primary
    AdaptedResistor   rloadRef_{1500.0f};     ///< Rload reflected to primary
    AdaptedCapacitor  cSecRef_{20.5e-9f};     ///< C_sec reflected to primary
    NonlinearLeaf     lm_;                    ///< Nonlinear magnetizing inductance

    // ═══════════════════════════════════════════════════════════════════════════
    // ADAPTORS
    // ═══════════════════════════════════════════════════════════════════════════

    // Source branch: Rs + Rdc_pri in series, then that + Lleak in series
    WDFSeriesAdaptor          sSourceInner_;  ///< Rs + Rdc_pri
    WDFSeriesAdaptor          sSourceOuter_;  ///< (Rs+Rdc_pri) + Lleak

    // Load branch: Rload_ref || C_sec_ref in parallel, then Rdc_sec_ref + that
    DynamicParallelAdaptor<2> pLoad_;         ///< Rload_ref || C_sec_ref
    WDFSeriesAdaptor          sLoad_;         ///< Rdc_sec_ref + pLoad_

    // Root junction: 4-port parallel
    //   port 0: sSourceOuter_ (source branch)
    //   port 1: cPriShield_   (primary-shield cap)
    //   port 2: sLoad_        (load branch)
    //   port 3: lm_           (nonlinear root leaf — ADAPTED PORT)
    DynamicParallelAdaptor<4> pRoot_;

    // ═══════════════════════════════════════════════════════════════════════════
    // STATE
    // ═══════════════════════════════════════════════════════════════════════════

    float turnsRatio_ = 10.0f;
    float sampleRate_ = 44100.0f;
    int   adaptCounter_ = 0;
};

} // namespace transfo
