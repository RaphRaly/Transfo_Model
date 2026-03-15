#pragma once

// =============================================================================
// WDFSeriesAdaptor — 3-port series junction for WDF circuits.
//
// Connects two child ports in series, presenting an adapted impedance
// R_adapted = R1 + R2 to the parent port.
//
// Scattering: b[i] = -a[i] + beta[i] * sum(a[j])
// where beta[i] = 2*R[i] / R_sum, R_sum = R1 + R2 + R_adapted = 2*(R1+R2)
//
// For the adapted port (port 3): b3 = a1 + a2 (always)
//
// Reference: Fettweis "Wave Digital Filters: Theory and Practice" 1986
// =============================================================================

#include <cmath>
#include "../util/Constants.h"

namespace transfo {

class WDFSeriesAdaptor
{
public:
    // ─── Construction ────────────────────────────────────────────────────────

    /// Construct with child port impedances R1, R2.
    /// The adapted (parent) port impedance is R1 + R2.
    WDFSeriesAdaptor(float R1 = 1000.0f, float R2 = 1000.0f)
    {
        setPortImpedances(R1, R2);
    }

    // ─── Port impedance configuration ────────────────────────────────────────

    /// Set both child port impedances and recompute scattering coefficients.
    void setPortImpedances(float R1, float R2)
    {
        R1_ = std::max(R1, kEpsilonF);
        R2_ = std::max(R2, kEpsilonF);
        R_adapted_ = R1_ + R2_;
        beta1_ = R1_ / R_adapted_;
        beta2_ = R2_ / R_adapted_;
    }

    /// Set an individual child port impedance (child = 0 or 1).
    void setChildImpedance(int child, float R)
    {
        if (child == 0)
            setPortImpedances(R, R2_);
        else
            setPortImpedances(R1_, R);
    }

    /// Get a child port impedance (child = 0 or 1).
    float getChildImpedance(int child) const
    {
        return (child == 0) ? R1_ : R2_;
    }

    /// Get the adapted (parent/upward) port impedance = R1 + R2.
    float getAdaptedImpedance() const
    {
        return R_adapted_;
    }

    // ─── Scattering ─────────────────────────────────────────────────────────

    /// Full 3-port scattering.
    ///
    /// Given incident waves a1 (child 1), a2 (child 2), a_parent (parent port),
    /// compute reflected waves b1, b2, b_parent using:
    ///   b[i] = -a[i] + beta[i] * (a1 + a2 + a_parent)
    ///
    /// For the adapted port (beta3 = 1): b_parent = a1 + a2.
    void scatter(float a1, float a2, float a_parent,
                 float& b1, float& b2, float& b_parent) const
    {
        const float a_sum = a1 + a2 + a_parent;
        b1 = -a1 + beta1_ * a_sum;
        b2 = -a2 + beta2_ * a_sum;
        b_parent = a1 + a2;
    }

    /// Scattering for the adapted port only (when this junction is root,
    /// a_parent = 0). Returns b_parent = a1 + a2.
    float scatterAdapted(float a1, float a2) const
    {
        return a1 + a2;
    }

    /// Downward scattering: compute child reflected waves given all three
    /// incident waves. Does not compute b_parent (caller already knows it).
    void scatterDown(float a1, float a2, float a_parent,
                     float& b1, float& b2) const
    {
        const float a_sum = a1 + a2 + a_parent;
        b1 = -a1 + beta1_ * a_sum;
        b2 = -a2 + beta2_ * a_sum;
    }

private:
    float R1_        = 1000.0f;
    float R2_        = 1000.0f;
    float R_adapted_ = 2000.0f;   // R1 + R2
    float beta1_     = 0.5f;      // R1 / (R1 + R2)
    float beta2_     = 0.5f;      // R2 / (R1 + R2)
};

} // namespace transfo
