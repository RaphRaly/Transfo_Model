#pragma once

// =============================================================================
// TopologicalJunction — N-port scattering junction for WDF circuits.
//
// Implements the general scattering matrix approach for connecting N WDF
// one-port elements. The scattering matrix S is pre-computed from the
// fundamental loop matrix B and port resistances Z:
//
//   S = I - 2 * Z * B^T * (B * Z * B^T)^{-1} * B
//
// where Z = diag(Z_1, ..., Z_N).
//
// Key optimization: S is recomputed only when a port resistance changes
// (not every sample). Rank-1 update via Sherman-Morrison when a single
// port resistance changes: O(N^2) instead of O(N^3).
//
// Confirmed by chowdsp_wdf (RTypeAdaptorBase.h): S pre-computed,
// updated event-driven. Matrix-vector product optionally SIMD.
//
// Reference: Fettweis 1986; DAFx-2015 'WDF adaptors for arbitrary
//            topologies'; Werner thesis Stanford ch.3-4
// =============================================================================

#include "../util/SmallMatrix.h"
#include "../util/Constants.h"
#include <array>
#include <cmath>

namespace transfo {

template <int N>
class TopologicalJunction
{
public:
    TopologicalJunction() { S_.identity(); }

    // ─── Setup: set fundamental loop matrix B ───────────────────────────────
    // B is p x N where p = number of independent loops.
    // For a tree with one adapted port, B is typically (N-1) x N.
    template <int P>
    void setFundamentalLoopMatrix(const SmallMatrix<float, P, N>& B)
    {
        // Store B dimensions — we assume P < N for now
        // Compute S from B and current Z values
        numLoops_ = P;

        // Copy B into internal storage (padded to N x N max)
        for (int r = 0; r < P; ++r)
            for (int c = 0; c < N; ++c)
                B_fund_(r, c) = B(r, c);

        recomputeScatteringMatrix();
    }

    // ─── Set port resistances ───────────────────────────────────────────────
    void setPortResistance(int portIndex, float Z)
    {
        if (portIndex >= 0 && portIndex < N)
        {
            float Z_old = Z_ports_[portIndex];
            Z_ports_[portIndex] = Z;

            if (std::abs(Z - Z_old) > kEpsilonF)
                needsUpdate_ = true;
        }
    }

    void setAdaptedPort(int portIndex) { adaptedPort_ = portIndex; }

    // ─── Compute scattering matrix S ────────────────────────────────────────
    // S = I - 2 * Z * B^T * (B * Z * B^T)^{-1} * B
    void computeScatteringMatrix()
    {
        recomputeScatteringMatrix();
        needsUpdate_ = false;
    }

    // ─── Forward scattering: compute b = S * a ──────────────────────────────
    // Called during HSIM forward sweep.
    void scatter(const float* a_ports, float* b_ports) const
    {
        S_.mulVec(a_ports, b_ports);
    }

    // ─── Get reflected wave at adapted port ─────────────────────────────────
    float scatterForward(const float* a_ports) const
    {
        float b = 0.0f;
        for (int c = 0; c < N; ++c)
            b += S_(adaptedPort_, c) * a_ports[c];
        return b;
    }

    // ─── Rank-1 update (Sherman-Morrison) when one port Z changes ───────────
    // O(N^2) instead of full O(N^3) recomputation.
    void updateRank1(int portIndex, float Z_old, float Z_new)
    {
        if (std::abs(Z_new - Z_old) < kEpsilonF)
            return;

        Z_ports_[portIndex] = Z_new;

        // Sherman-Morrison: S_new = S_old + (dZ / (1 + dZ * S_old[p,p])) * col_p * row_p
        // Simplified for diagonal Z change:
        const float dZ = Z_new - Z_old;

        // Extract column p and row p of S
        float col_p[N], row_p[N];
        for (int i = 0; i < N; ++i)
        {
            col_p[i] = S_(i, portIndex);
            row_p[i] = S_(portIndex, i);
        }

        const float denom = 1.0f + dZ * S_(portIndex, portIndex);
        if (std::abs(denom) < kEpsilonF)
        {
            // Fallback to full recomputation
            recomputeScatteringMatrix();
            return;
        }

        const float scale = dZ / denom;
        S_.rank1Update(col_p, row_p, scale);

        needsUpdate_ = false;
    }

    // ─── Status ─────────────────────────────────────────────────────────────
    bool needsUpdate() const { return needsUpdate_; }

    const SmallMatrix<float, N, N>& getScatteringMatrix() const { return S_; }
    float getPortResistance(int i) const { return Z_ports_[i]; }

    static constexpr int numPorts() { return N; }

private:
    SmallMatrix<float, N, N> S_;            // Scattering matrix
    SmallMatrix<float, N, N> B_fund_;       // Fundamental loop matrix (padded)
    std::array<float, N>     Z_ports_{};    // Port resistances
    int                      adaptedPort_ = 0;
    int                      numLoops_ = N - 1;
    bool                     needsUpdate_ = true;

    void recomputeScatteringMatrix()
    {
        // S = I - 2 * Z * B^T * (B * Z * B^T)^{-1} * B
        // This is the general R-type adaptor computation.

        // Build diagonal Z matrix elements (used inline)
        // Compute B * Z * B^T  (numLoops x numLoops)
        SmallMatrix<float, N, N> BZBt;
        for (int r = 0; r < numLoops_; ++r)
            for (int c = 0; c < numLoops_; ++c)
            {
                float sum = 0.0f;
                for (int k = 0; k < N; ++k)
                    sum += B_fund_(r, k) * Z_ports_[k] * B_fund_(c, k);
                BZBt(r, c) = sum;
            }

        // Invert (B * Z * B^T)
        SmallMatrix<float, N, N> BZBt_inv = BZBt;
        BZBt_inv.invert();

        // S = I - 2 * Z * B^T * (BZBt)^{-1} * B
        S_.identity();
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
            {
                float val = 0.0f;
                for (int p = 0; p < numLoops_; ++p)
                    for (int q = 0; q < numLoops_; ++q)
                        val += Z_ports_[i] * B_fund_(p, i) * BZBt_inv(p, q) * B_fund_(q, j);
                S_(i, j) -= 2.0f * val;
            }

        needsUpdate_ = false;
    }
};

} // namespace transfo
