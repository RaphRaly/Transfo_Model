#pragma once

// =============================================================================
// DynamicParallelAdaptor — N-port parallel junction adaptor with dynamic
// impedance update support for Wave Digital Filter networks.
//
// All N ports see the same voltage V at the junction node.
//
// Given port resistances R[i], conductances G[i] = 1/R[i],
// and G_sum = sum(G[i]):
//
//   Scattering coefficients:  alpha[i] = 2 * G[i] / G_sum
//   Property:                 sum(alpha[i]) = 2
//   Junction voltage:         V = sum(alpha[i] * a[i]) / 2
//   Scattering equation:      b[i] = sum_j(alpha[j] * a[j]) - a[i]
//   Adapted impedance:        R_adapted = 1 / G_sum
//
// Supports dynamic impedance changes via setPortImpedance() followed by
// recalculateScattering(). Designed for LC parasitic resonance filtering
// and dynamic magnetizing inductance in transformer audio models.
//
// Template parameter N is the number of ports (default 3).
//
// Reference: Fettweis 1986; chowdsp_wdf
// =============================================================================

#include <array>
#include <cmath>
#include "../util/Constants.h"

namespace transfo {

// ─── Dynamic Parallel Adaptor ─────────────────────────────────────────────
// N-port parallel junction with runtime impedance updates.
template <int N = 3>
class DynamicParallelAdaptor
{
public:
    DynamicParallelAdaptor()
    {
        R_.fill(1000.0f);
        recalculateScattering();
    }

    // ─── Port Impedance ───────────────────────────────────────────────────

    /// Set port impedance for a single port (marks scattering as dirty).
    void setPortImpedance(int port, float R)
    {
        R_[port] = (R < kEpsilonF) ? kEpsilonF : R;
        dirty_ = true;
    }

    /// Get current port impedance.
    float getPortImpedance(int port) const
    {
        return R_[port];
    }

    // ─── Scattering Coefficients ──────────────────────────────────────────

    /// Recompute alpha[] coefficients from current R[] values.
    /// Called after one or more setPortImpedance() calls.
    ///
    /// Uses double-precision accumulation to avoid precision loss when port
    /// impedances span many orders of magnitude (e.g. Rload=600 Ohm vs
    /// Lm=2.9 MOhm in the JT-11ELCF output transformer).
    void recalculateScattering()
    {
        // Accumulate conductances in double to avoid catastrophic loss
        // when small G values (high-Z ports) are summed with large ones.
        double G_sum_d = 0.0;
        for (int i = 0; i < N; ++i)
        {
            G_d_[i] = 1.0 / static_cast<double>(R_[i]);
            G_sum_d += G_d_[i];
        }

        G_sum_ = static_cast<float>(G_sum_d);

        const double invGSum = 1.0 / G_sum_d;
        for (int i = 0; i < N; ++i)
        {
            alpha_d_[i] = 2.0 * G_d_[i] * invGSum;
            alpha_[i] = static_cast<float>(alpha_d_[i]);
            // Precompute (alpha[i] - 1) for cancellation-free scattering.
            // Since alpha[i] = 2*G[i]/G_sum, we have:
            //   alpha[i] - 1 = (2*G[i] - G_sum) / G_sum
            // Computing it this way in double avoids losing precision
            // when alpha[i] is close to 0 or 2.
            alphaM1_d_[i] = (2.0 * G_d_[i] - G_sum_d) * invGSum;
        }

        dirty_ = false;
    }

    // ─── Scattering ──────────────────────────────────────────────────────

    /// Perform scattering: compute b[] from a[].
    ///
    /// Mathematically: b[i] = sum_j(alpha[j] * a[j]) - a[i]
    ///
    /// Numerically stable form (avoids catastrophic cancellation when
    /// alpha[i] is near 0 or near 2):
    ///   b[i] = (alpha[i] - 1) * a[i] + sum_{j!=i}(alpha[j] * a[j])
    ///
    /// Uses double-precision accumulation throughout to handle impedance
    /// ratios of 10000:1 and beyond (e.g. T2 output transformer at 600 Ohm).
    void scatter(const float* a, float* b) const
    {
        for (int i = 0; i < N; ++i)
        {
            double bi = alphaM1_d_[i] * static_cast<double>(a[i]);
            for (int j = 0; j < N; ++j)
            {
                if (j != i)
                    bi += alpha_d_[j] * static_cast<double>(a[j]);
            }
            b[i] = static_cast<float>(bi);
        }
    }

    /// Single-port scatter: compute b for adapted port only.
    /// Useful when only one port output is needed.
    ///
    /// Uses the cancellation-free formula:
    ///   b[adapted] = (alpha[adapted] - 1) * a[adapted]
    ///              + sum_{j != adapted}(alpha[j] * a[j])
    float scatterAdapted(const float* a) const
    {
        const int ap = adaptedPort_;
        double b = alphaM1_d_[ap] * static_cast<double>(a[ap]);
        for (int j = 0; j < N; ++j)
        {
            if (j != ap)
                b += alpha_d_[j] * static_cast<double>(a[j]);
        }
        return static_cast<float>(b);
    }

    // ─── Adapted Port ─────────────────────────────────────────────────────

    /// Get adapted (upward-facing) impedance = 1/G_sum.
    float getAdaptedImpedance() const
    {
        return 1.0f / G_sum_;
    }

    /// Set which port is adapted (default: last port, N-1).
    void setAdaptedPort(int port)
    {
        adaptedPort_ = port;
    }

    /// Get adapted port index.
    int getAdaptedPort() const
    {
        return adaptedPort_;
    }

    // ─── Status ───────────────────────────────────────────────────────────

    /// Check if recalculation is needed.
    bool needsUpdate() const
    {
        return dirty_;
    }

    /// Validate: check sum(alpha) is approximately 2 (using double-precision).
    bool isValid() const
    {
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            sum += alpha_d_[i];
        }
        return std::fabs(sum - 2.0) < 1e-10;
    }

private:
    std::array<float, N>  R_;          // Port resistances (float, set by user)
    std::array<double, N> G_d_;        // Port conductances in double (1/R)
    std::array<float, N>  alpha_;      // Scattering coefficients (float cache)
    std::array<double, N> alpha_d_;    // Scattering coefficients (double, used in scatter)
    std::array<double, N> alphaM1_d_;  // (alpha[i] - 1) in double, for cancellation-free scatter
    float G_sum_ = 0.0f;              // Sum of conductances (float cache)
    int adaptedPort_ = N - 1;         // Which port is adapted
    bool dirty_ = true;               // Need to recalculate
};

} // namespace transfo
