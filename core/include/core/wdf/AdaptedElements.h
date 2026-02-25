#pragma once

// =============================================================================
// AdaptedElements — Linear adapted WDF one-port elements.
//
// All elements derive from WDOnePort via CRTP.
// "Adapted" means the port resistance Z is chosen to simplify scattering:
//   - Adapted resistor:   Z = R, b = 0
//   - Adapted capacitor:  Z = Ts/(2C), b = state_  (trapezoidal)
//   - Adapted inductor:   Z = 2L/Ts, b = -state_  (trapezoidal)
//   - Adapted V source:   b = Vg (voltage source)
//   - Adapted R source:   Z = Rs, b = Vg
//
// Trapezoidal discretization ensures passivity.
//
// Reference: Fettweis 1986; chowdsp_wdf wdf_one_ports.h
// =============================================================================

#include "WDOnePort.h"

namespace transfo {

// ─── Adapted Resistor ───────────────────────────────────────────────────────
// Z = R, b = 0 (all energy absorbed)
class AdaptedResistor : public WDOnePort<AdaptedResistor>
{
public:
    explicit AdaptedResistor(float R = 1000.0f)
    {
        setResistance(R);
    }

    void setResistance(float R)
    {
        R_ = R;
        Z_port_ = R;
    }

    float scatterImpl(float /*a*/) { return 0.0f; }
    float getPortResistanceImpl() const { return Z_port_; }

private:
    float R_ = 1000.0f;
};

// ─── Adapted Capacitor ──────────────────────────────────────────────────────
// Trapezoidal discretization: Z = Ts/(2C), b[n] = a[n-1] (unit delay)
// State is the previous incident wave.
class AdaptedCapacitor : public WDOnePort<AdaptedCapacitor>
{
public:
    explicit AdaptedCapacitor(float C = 1e-9f, float Ts = 1.0f / 44100.0f)
    {
        setCapacitance(C, Ts);
    }

    void setCapacitance(float C, float Ts)
    {
        C_ = C;
        Ts_ = Ts;
        Z_port_ = Ts / (2.0f * C);
    }

    void prepare(float sampleRate)
    {
        Ts_ = 1.0f / sampleRate;
        Z_port_ = Ts_ / (2.0f * C_);
    }

    float scatterImpl(float a)
    {
        float b = state_;
        state_ = a;
        return b;
    }

    float getPortResistanceImpl() const { return Z_port_; }

    void reset() { state_ = 0.0f; }

private:
    float C_     = 1e-9f;
    float Ts_    = 1.0f / 44100.0f;
    float state_ = 0.0f;   // a[n-1]
};

// ─── Adapted Inductor ───────────────────────────────────────────────────────
// Trapezoidal discretization: Z = 2L/Ts, b[n] = -a[n-1]
class AdaptedInductor : public WDOnePort<AdaptedInductor>
{
public:
    explicit AdaptedInductor(float L = 10.0f, float Ts = 1.0f / 44100.0f)
    {
        setInductance(L, Ts);
    }

    void setInductance(float L, float Ts)
    {
        L_ = L;
        Ts_ = Ts;
        Z_port_ = 2.0f * L / Ts;
    }

    void prepare(float sampleRate)
    {
        Ts_ = 1.0f / sampleRate;
        Z_port_ = 2.0f * L_ / Ts_;
    }

    float scatterImpl(float a)
    {
        float b = -state_;
        state_ = a;
        return b;
    }

    float getPortResistanceImpl() const { return Z_port_; }

    void reset() { state_ = 0.0f; }

private:
    float L_     = 10.0f;
    float Ts_    = 1.0f / 44100.0f;
    float state_ = 0.0f;   // a[n-1]
};

// ─── Adapted Voltage Source ─────────────────────────────────────────────────
// Ideal voltage source: V = Vg regardless of current.
// b = Vg (reflected wave equals source voltage in wave domain)
class AdaptedVSource : public WDOnePort<AdaptedVSource>
{
public:
    explicit AdaptedVSource(float Vg = 0.0f)
        : Vg_(Vg)
    {
        Z_port_ = 1e-3f; // Near-zero source impedance
    }

    void setVoltage(float Vg) { Vg_ = Vg; }

    float scatterImpl(float /*a*/) { return Vg_; }
    float getPortResistanceImpl() const { return Z_port_; }

private:
    float Vg_ = 0.0f;
};

// ─── Adapted Resistive Source (Thevenin) ────────────────────────────────────
// Voltage source with series resistance: V = Vg, Z = Rs
// b = Vg (when adapted to Rs, reflected wave = source signal)
class AdaptedRSource : public WDOnePort<AdaptedRSource>
{
public:
    explicit AdaptedRSource(float Rs = 150.0f, float Vg = 0.0f)
        : Rs_(Rs), Vg_(Vg)
    {
        Z_port_ = Rs;
    }

    void setSourceImpedance(float Rs)
    {
        Rs_ = Rs;
        Z_port_ = Rs;
    }

    void setVoltage(float Vg) { Vg_ = Vg; }

    float scatterImpl(float /*a*/) { return Vg_; }
    float getPortResistanceImpl() const { return Z_port_; }

private:
    float Rs_ = 150.0f;
    float Vg_ = 0.0f;
};

} // namespace transfo
