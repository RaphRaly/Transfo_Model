#pragma once

// =============================================================================
// DCBlocker — First-order high-pass filter to remove DC offset.
//
// The hysteresis model can introduce DC offset due to the asymmetric
// nature of the B-H loop and the dB/dt output. A simple DC blocker
// (first-order HPF at ~5 Hz) cleans this up.
//
// Transfer function: H(z) = (1 - z^-1) / (1 - R*z^-1)
// where R = 1 - 2*pi*fc/fs (cutoff frequency fc ≈ 5 Hz)
// =============================================================================

class DCBlocker
{
public:
    DCBlocker() = default;

    void prepare(double sampleRate)
    {
        // Cutoff around 5 Hz — removes DC without affecting audible content
        const double fc = 5.0;
        R = 1.0 - (2.0 * 3.14159265358979323846 * fc / sampleRate);
    }

    void reset()
    {
        x1 = 0.0;
        y1 = 0.0;
    }

    double process(double input)
    {
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        const double output = input - x1 + R * y1;
        x1 = input;
        y1 = output;
        return output;
    }

private:
    double R  = 0.9999;   // Pole radius — set in prepare()
    double x1 = 0.0;      // Previous input
    double y1 = 0.0;      // Previous output
};
