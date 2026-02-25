#pragma once

// =============================================================================
// WDOnePort — CRTP base class for all one-port WDF elements.
//
// Wave Digital Filters represent circuit elements through incident (a) and
// reflected (b) wave variables, with a port resistance Z.
//
// Kirchhoff voltage: V = (a + b) / 2
// Kirchhoff current: I = (a - b) / (2 * Z)
//
// CRTP ensures inline dispatch on the hot path (critical at 176.4 kHz with
// 3 reluctances x 4 iterations = 2.1M calls/s).
//
// Derived classes must implement:
//   - scatterImpl(float a) -> float b
//   - getPortResistanceImpl() -> float
//
// Reference: chowdsp_wdf (Chowdhury arXiv:2210.12554);
//            Fettweis "Wave Digital Filters: Theory and Practice" 1986
// =============================================================================

#include <cmath>

namespace transfo {

template <typename Derived>
class WDOnePort
{
public:
    // ─── Scattering: compute reflected wave b from incident wave a ──────────
    // Inline CRTP dispatch — no virtual call overhead.
    inline float scatter(float a)
    {
        a_incident_ = a;
        b_reflected_ = static_cast<Derived*>(this)->scatterImpl(a);
        return b_reflected_;
    }

    // ─── Port resistance ────────────────────────────────────────────────────
    float getPortResistance() const
    {
        return static_cast<const Derived*>(this)->getPortResistanceImpl();
    }

    // ─── Kirchhoff domain conversion ────────────────────────────────────────
    float getKirchhoffV() const
    {
        return (a_incident_ + b_reflected_) * 0.5f;
    }

    float getKirchhoffI() const
    {
        const float Z = getPortResistance();
        return (Z > 1e-10f) ? (a_incident_ - b_reflected_) / (2.0f * Z) : 0.0f;
    }

    // ─── Wave variable access ───────────────────────────────────────────────
    float getIncidentWave()  const { return a_incident_; }
    float getReflectedWave() const { return b_reflected_; }

    void setIncidentWave(float a) { a_incident_ = a; }

protected:
    float Z_port_      = 1.0f;      // Port resistance
    float a_incident_  = 0.0f;      // Incident wave
    float b_reflected_ = 0.0f;      // Reflected wave
};

// ─── Linear one-port implementations ────────────────────────────────────────
// Each is a separate CRTP class for full inlining.

} // namespace transfo
