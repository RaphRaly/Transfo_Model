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
    void recalculateScattering()
    {
        G_sum_ = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            G_[i] = 1.0f / R_[i];
            G_sum_ += G_[i];
        }

        const float invGSum = 1.0f / G_sum_;
        for (int i = 0; i < N; ++i)
        {
            alpha_[i] = 2.0f * G_[i] * invGSum;
        }

        dirty_ = false;
    }

    // ─── Scattering ──────────────────────────────────────────────────────

    /// Perform scattering: compute b[] from a[].
    /// b[i] = sum_j(alpha[j] * a[j]) - a[i]
    void scatter(const float* a, float* b) const
    {
        // Compute common sum S = sum_j(alpha[j] * a[j])
        float S = 0.0f;
        for (int j = 0; j < N; ++j)
        {
            S += alpha_[j] * a[j];
        }

        for (int i = 0; i < N; ++i)
        {
            b[i] = S - a[i];
        }
    }

    /// Single-port scatter: compute b for adapted port only.
    /// Useful when only one port output is needed.
    float scatterAdapted(const float* a) const
    {
        float S = 0.0f;
        for (int j = 0; j < N; ++j)
        {
            S += alpha_[j] * a[j];
        }
        return S - a[adaptedPort_];
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

    /// Validate: check sum(alpha) is approximately 2.
    bool isValid() const
    {
        float sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            sum += alpha_[i];
        }
        return std::fabs(sum - 2.0f) < 1e-5f;
    }

private:
    std::array<float, N> R_;       // Port resistances
    std::array<float, N> G_;       // Port conductances (1/R)
    std::array<float, N> alpha_;   // Scattering coefficients
    float G_sum_ = 0.0f;           // Sum of conductances
    int adaptedPort_ = N - 1;      // Which port is adapted
    bool dirty_ = true;            // Need to recalculate
};

} // namespace transfo
