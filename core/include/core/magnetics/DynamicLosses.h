#pragma once

// =============================================================================
// DynamicLosses — Classical eddy current and excess (anomalous) losses
//                 (Bertotti field separation).
//
// The J-A static model captures hysteresis loss only. Real transformer cores
// also exhibit:
//   1. Classical eddy current losses: proportional to (dB/dt)^2
//   2. Excess/anomalous losses: proportional to |dB/dt|^1.5
//
// Total loss separation (Bertotti):
//   P_total = P_hysteresis + P_classical + P_excess
//
// In the time domain, these appear as additional H contributions:
//   H_dyn = K1 * dB/dt + K2 * |dB/dt|^0.5 * sign(dB/dt)
//
// where:
//   K1 = K_eddy = d^2 / (12 * rho)   [s/m]
//   K2 = K_exc                         [A·m^-1·(T/s)^-0.5]
//
// Two discretisations of Ḃ coexist by design:
//   1. Backward difference  — exposed via computeDBdt() / computeJacobian() /
//      computeHdynamic() / computeFieldSeparated(). Used by standalone tests
//      and identification code that drive DynamicLosses without an iterative
//      solver. Stable, first-order, no Nyquist pole.
//   2. Trapezoidal (bilinear) — internal to evalWithJacobianForNR(). Used by
//      the HysteresisProcessor J-A NR residual for time-reversal symmetry
//      with the trapezoidal J-A integrator (Perplexity Deep Research §5).
//
// commitState() advances BOTH the backward-diff B_prev and the trapezoidal
// Ḃ_prev so the two paths stay internally consistent without either one
// imposing its scheme on the other's clients.
//
// K1 and K2 are identified in Phase 2 of the identification pipeline.
//
// Reference: Bertotti 1998; Baghel & Kulkarni IEEE Trans. Magn. 2014;
//            Perplexity Deep Research synthesis §3.2, §5.
// =============================================================================

#include <cmath>
#include <algorithm>

namespace transfo {

class DynamicLosses
{
public:
    DynamicLosses() = default;

    void setCoefficients(float K1, float K2)
    {
        K1_ = std::max(0.0, static_cast<double>(K1));
        K2_ = std::max(0.0, static_cast<double>(K2));
        enabled_ = (K1_ > 0.0) || (K2_ > 0.0);
    }

    void setSampleRate(double sampleRate)
    {
        // Reset state when sample rate changes — stale B_prev / dBdt_prev at the
        // old rate would produce a transient spike on the first sample.
        if (sampleRate != sampleRate_) {
            B_prev_committed_    = 0.0;
            B_prev_backup_       = 0.0;
            dBdt_prev_committed_ = 0.0;
            dBdt_prev_backup_    = 0.0;
        }
        sampleRate_ = sampleRate;
    }

    bool isEnabled() const { return enabled_; }

    void reset()
    {
        B_prev_committed_    = 0.0;
        B_prev_backup_       = 0.0;
        dBdt_prev_committed_ = 0.0;
        dBdt_prev_backup_    = 0.0;
    }

    // ─── Compute H_dynamic from a pre-computed dB/dt ────────────────────────
    // This is the preferred entry point when dBdt is already known
    // (e.g. estimated at the leaf level from wave variables).
    double computeHfromDBdt(double dBdt) const
    {
        // Classical eddy loss: H_eddy = K1 * dB/dt
        const double H_eddy = K1_ * dBdt;

        // Excess loss: H_excess = K2 * sqrt(|dB/dt|) * sign(dB/dt)
        const double absdBdt = std::abs(dBdt);
        const double sign_dBdt = (dBdt > 0.0) ? 1.0 : (dBdt < 0.0) ? -1.0 : 0.0;
        const double H_excess = K2_ * sign_dBdt * std::sqrt(absdBdt + kEpsSqrt);

        return H_eddy + H_excess;
    }

    // ─── Backward difference derivative (public / test-facing) ─────────────
    // dBdt[n] = fs · (B[n] − B[n−1])
    //
    // First-order, dissipative, no Nyquist pole. Pure function of B_current
    // and committed B_prev (no mutation — safe to call during outer iteration).
    double computeDBdt(double B_current) const
    {
        return sampleRate_ * (B_current - B_prev_committed_);
    }

    // ─── Compute H_dynamic from B_current (backward-diff dB/dt) ────────────
    double computeHdynamic(double B_current) const
    {
        return computeHfromDBdt(computeDBdt(B_current));
    }

    // ─── NR Jacobian: dH_dynamic / dB (backward-diff, test-facing) ─────────
    // Both terms contribute; with backward Ḃ:
    //   d(dBdt)/dB = sampleRate
    //   dH_eddy/dB   = K1 * fs
    //   dH_excess/dB = min( K2 * 0.5 / sqrt(|dBdt| + eps), slopeCap ) * fs
    //
    // The excess slope is capped to prevent NR stiffness blowing up near
    // Ḃ=0 crossings (the sqrt singularity). eps regularises the same region
    // — both are needed together to keep NR well-behaved on signals with
    // significant Nyquist-band content (white noise, HF sines).
    double computeJacobian(double dBdt) const
    {
        const double dDBdt_dB = sampleRate_;            // backward diff

        const double dH_eddy_dB = K1_ * dDBdt_dB;

        const double absdBdt = std::abs(dBdt);
        double excess_slope = K2_ * 0.5 / std::sqrt(absdBdt + kEpsJac);
        if (excess_slope > kExcessSlopeCap) excess_slope = kExcessSlopeCap;
        const double dH_exc_dB = excess_slope * dDBdt_dB;

        return dH_eddy_dB + dH_exc_dB;
    }

    // ─── NR-coupled Jacobian: {H_dyn, dH_dyn/dM} at a trial B ──────────────
    //
    // The J-A solver's unknown is M, with B = μ₀·(H_appl + M). For the
    // field-separation residual H_eff = H_appl − H_dyn(Ḃ(M)), the analytical
    // Jacobian contribution is:
    //
    //   dH_dyn/dM = dH_dyn/d(Ḃ) · d(Ḃ)/dB · dB/dM
    //             = (K1 + K2 / (2·√(|Ḃ|+ε))) · (2·fs) · μ₀
    //
    // Reference: Perplexity Deep Research §3.3 (analytical Jacobian).
    struct NRJacobian {
        double H_dyn;      // H_dyn( Ḃ(B_current) )        [A/m]
        double dH_dyn_dM;  // d(H_dyn) / d(M)              [A/m per A/m]
    };

    NRJacobian evalWithJacobianForNR(double B_current) const
    {
        NRJacobian j{};
        if (!enabled_) { return j; }

        // Trapezoidal (bilinear) Ḃ — used only on the NR residual path,
        // independent of the backward-diff public computeDBdt() above.
        const double dBdt_trap = 2.0 * sampleRate_ * (B_current - B_prev_committed_)
                                 - dBdt_prev_committed_;
        j.H_dyn = computeHfromDBdt(dBdt_trap);

        const double absdBdt = std::abs(dBdt_trap);
        // Excess slope with regularisation + physical cap. Both are needed:
        // eps smooths the sqrt near zero; the cap bounds the slope ceiling so
        // NR never faces |Δr'| ≫ 1 when Ḃ transits through zero (white-noise
        // / HF sine pathologies). See Perplexity Deep Research §3.2.
        double excess_slope = K2_ * 0.5 / std::sqrt(absdBdt + kEpsJac);
        if (excess_slope > kExcessSlopeCap) excess_slope = kExcessSlopeCap;
        const double dHdyn_d_dBdt = K1_ + excess_slope;

        //  d(Ḃ_trap)/dM = d(Ḃ_trap)/dB · dB/dM
        //               = (2·fs)       · μ₀
        constexpr double mu_0 = 1.2566370614e-6;
        const double d_dBdt_dM = 2.0 * sampleRate_ * mu_0;

        j.dH_dyn_dM = dHdyn_d_dBdt * d_dBdt_dM;
        return j;
    }

    // ─── State management ──────────────────────────────────────────────────
    //
    // commitState(B): lock in the current B as the new B_prev for next sample.
    // Also advances the trapezoidal Ḃ state: Ḃ_prev <- Ḃ_n computed from the
    // committed B via the trapezoidal formula. This keeps the internal Ḃ
    // state consistent with the advertised discretization.
    void commitState(double B_committed)
    {
        const double dBdt_n = 2.0 * sampleRate_ * (B_committed - B_prev_committed_)
                              - dBdt_prev_committed_;
        dBdt_prev_committed_ = dBdt_n;
        B_prev_committed_    = B_committed;
    }

    // Seed the committed state without running the trapezoidal update. Used at
    // bootstrap to align B_prev with the initial flux (B = μ₀·H at M=0) while
    // keeping Ḃ_prev at zero — prevents a spurious spike on the first real
    // solver call.
    void seedCommittedState(double B_initial, double dBdt_initial = 0.0)
    {
        B_prev_committed_    = B_initial;
        B_prev_backup_       = B_initial;
        dBdt_prev_committed_ = dBdt_initial;
        dBdt_prev_backup_    = dBdt_initial;
    }

    // savePrevState / restorePrevState: snapshot for iterative rollback
    // (retained for WDF-style outer iteration compatibility).
    void savePrevState()
    {
        B_prev_backup_    = B_prev_committed_;
        dBdt_prev_backup_ = dBdt_prev_committed_;
    }

    void restorePrevState()
    {
        B_prev_committed_    = B_prev_backup_;
        dBdt_prev_committed_ = dBdt_prev_backup_;
    }

    // ─── Field-separated Bertotti correction (legacy post-hoc path) ────────
    //
    // ⚠️ DEPRECATED (Sprint A2 Voie C, 2026-04-30) — superseded by the
    // pre-J-A H_eff path in TransformerModel::processSample which uses
    // computeHfromDBdt + Baghel-Kulkarni implicit decoupling. Retained for:
    //   1. Tests/test_bertotti_field_separation.cpp (validates the
    //      computeFieldSeparated API in isolation).
    //   2. JilesAthertonLeaf::scatter legacy magnetic-domain path (K_geo<=0
    //      branch) — dead in cascade-only architecture but still compiles.
    //   3. Future identification pipelines that may still want the post-hoc
    //      correction shape.
    //
    // For new code, prefer:
    //   const double dBdt_raw = dyn.computeDBdt(B_pred);  // backward-diff
    //   const double G  = dyn.getK1() * fs * mu0 * chi;
    //   const double dBdt = dBdt_raw * (1 + chi) / (1 + G);
    //   const double Hdyn = dyn.computeHfromDBdt(dBdt);   // signed H field
    //
    // Reference: Baghel & Kulkarni IEEE Trans. Magn. 2014 (field separation);
    //            Mousavi & Engdahl IET CEM 2014 (local linearization).
    struct FieldSepResult {
        double H_dyn;           // Raw dynamic field [A/m]
        double H_dyn_clamped;   // Safety-clamped dynamic field
        double B_correction;    // Additive correction to B_static
    };

    [[deprecated("A2 Voie C: prefer computeHfromDBdt + Baghel-Kulkarni "
                 "decoupling; see TransformerModel::processSample for the "
                 "pattern. Retained for test/identification compatibility.")]]
    FieldSepResult computeFieldSeparated(double B_static, double H_applied,
                                          double cascadeEddyFactor = 1.0) const
    {
        FieldSepResult r{};

        // 1. dB_static/dt via backward difference from committed B (public
        //    path — same scheme as computeDBdt()).
        const double dBdt_static = sampleRate_ * (B_static - B_prev_committed_);

        // 2. Compute raw dynamic field
        r.H_dyn = computeHfromDBdt(dBdt_static);

        // 3. Safety clamp: prevent |H_dyn| from exceeding 80% of |H_applied|.
        //    Structural guarantee against sign inversion.
        constexpr double kSafetyFactor = 0.8;
        r.H_dyn_clamped = r.H_dyn;
        const double H_limit = kSafetyFactor * std::abs(H_applied);
        if (std::abs(r.H_dyn) > H_limit && std::abs(H_applied) > 1e-10)
        {
            r.H_dyn_clamped = std::copysign(H_limit, r.H_dyn);
        }

        // 4. B correction: the dynamic field widens the B-H loop.
        //    ΔB = μ₀ × H_dyn_clamped × cascadeEddyFactor
        r.B_correction = 1.2566370614359173e-6  // μ₀
                        * r.H_dyn_clamped * cascadeEddyFactor;

        return r;
    }

    // ─── Legacy API (backward compatibility with ObjectiveFunction) ─────────
    // Alias for commitState() — used by identification pipeline.
    void updateState(double B_current)
    {
        commitState(B_current);
    }

    // ─── Accessors ─────────────────────────────────────────────────────────
    double getK1() const { return K1_; }
    double getK2() const { return K2_; }
    double getBprevCommitted() const { return B_prev_committed_; }
    double getDBdtPrevCommitted() const { return dBdt_prev_committed_; }
    double getSampleRate() const { return sampleRate_; }

    // ─── Helper: compute K_eddy from physical material properties ──────────
    // d = lamination thickness [m], rho = resistivity [Ohm·m]
    static float computeKeddy(float d_meters, float rho_ohm_m)
    {
        if (rho_ohm_m <= 0.0f) return 0.0f;
        return (d_meters * d_meters) / (12.0f * rho_ohm_m);
    }

private:
    double K1_ = 0.0;               // Classical eddy coefficient (K_eddy)
    double K2_ = 0.0;               // Excess loss coefficient   (K_exc)
    double sampleRate_ = 44100.0;
    bool   enabled_ = false;

    // Double-buffered state for trapezoidal Ḃ discretization.
    // Both B_prev and dBdt_prev must stay in sync — advanced together in commitState.
    double B_prev_committed_    = 0.0; // B[n-1] confirmed
    double B_prev_backup_       = 0.0; // Snapshot for rollback
    double dBdt_prev_committed_ = 0.0; // Ḃ[n-1] confirmed
    double dBdt_prev_backup_    = 0.0; // Snapshot for rollback

    // Epsilon for sqrt(0) safety in the function value.
    static constexpr double kEpsSqrt = 1e-12;

    // Epsilon for the Jacobian sqrt denominator.
    // Larger than kEpsSqrt to keep NR Jacobian finite and robust near dBdt=0.
    // Raised from 1e-6 → 1e-4 to tame the excess-term stiffness at Nyquist-
    // pathological inputs (white noise, full-scale HF sines). See Deep Research
    // Intégration Bertotti §3.2 + option 5 follow-up.
    static constexpr double kEpsJac = 1e-4;

    // Ceiling on the excess contribution to dH_dyn/d(Ḃ). Prevents the
    // K2/(2·sqrt(|Ḃ|+eps)) term from spiking the NR Jacobian to values where
    // |Δr'| ≫ 1 forces tiny, slow Newton steps. 50 s/m is physically generous
    // — for the JT-115K-E preset (K2=0.05) the uncapped slope only reaches
    // this ceiling for |Ḃ| below ~2.5e-7 T/s, well inside the zero-crossing
    // noise band.
    static constexpr double kExcessSlopeCap = 50.0;
};

} // namespace transfo
