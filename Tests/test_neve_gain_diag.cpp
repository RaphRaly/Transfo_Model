// Quick diagnostic for Neve1063Path gain
#include <cmath>
#include <cstdio>
#include "core/preamp/Neve1063Path.h"
#include "core/preamp/GainTable.h"
#include "core/model/PreampConfig.h"

int main()
{
    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amp = 0.001f; // 1mV
    const float Ts = 1.0f / sampleRate;

    transfo::Neve1063Path path;
    transfo::Neve1063PathConfig config;
    path.configure(config);
    path.prepare(sampleRate, 512);

    // Position 5: Rfb=1430, expected Acl=31.43 (29.9 dB)
    float rfb = transfo::GainTable::getRfb(5);
    path.setGain(rfb);
    path.reset();

    printf("=== Neve Gain Diagnostic ===\n");
    printf("Rfb = %.1f Ohm, Acl = %.2f (%.1f dB)\n",
           rfb, 1.0f + rfb / 47.0f, 20.0f * std::log10(1.0f + rfb / 47.0f));

    // Run 2000 samples, print diagnostics at specific points
    float peakIn = 0, peakOut = 0;
    double sumSqIn = 0, sumSqOut = 0;
    int measStart = 480; // skip 5 cycles

    for (int i = 0; i < 2000; ++i)
    {
        float t = static_cast<float>(i) * Ts;
        float input = amp * std::sin(2.0f * 3.14159265f * freq * t);
        float output = path.processSample(input);

        if (i >= measStart)
        {
            sumSqIn += input * input;
            sumSqOut += output * output;
        }

        // Print at positive peak of 10th cycle (~sample 960)
        if (i == 960)
        {
            auto op = path.getOperatingPoint();
            float Aol = std::abs(path.getOpenLoopGain());
            float Acl = 1.0f + rfb / 47.0f;

            printf("\n--- Sample %d (peak of 10th cycle) ---\n", i);
            printf("Input          = %.6f V\n", input);
            printf("Output         = %.6f V\n", output);
            printf("Gain (linear)  = %.2f (%.1f dB)\n",
                   std::abs(output / input),
                   20.0f * std::log10(std::abs(output / input) + 1e-30f));
            printf("\nNV-TR4: Vce=%.3f V, Ic=%.4f mA, gm=%.3f mS\n",
                   op.Vce_nv4, op.Ic_nv4 * 1000, op.Ic_nv4 > 0 ? op.Ic_nv4 / 0.026f * 1000 : 0.0f);
            printf("AM-TR1: Vce=%.3f V, Ic=%.4f mA, gm=%.3f mS\n",
                   op.Vce_am1, op.Ic_am1 * 1000, std::abs(op.Ic_am1) / 0.026f * 1000);
            printf("AM-TR3: Vce=%.3f V, Ic=%.4f mA\n",
                   op.Vce_am3, op.Ic_am3 * 1000);
            printf("\nAol (getOpenLoopGain) = %.2f (%.1f dB)\n",
                   Aol, 20.0f * std::log10(Aol + 1e-30f));
            printf("Acl (1+Rfb/Rg)       = %.2f (%.1f dB)\n",
                   Acl, 20.0f * std::log10(Acl));
            printf("Acl/Aol              = %.4f\n", Acl / (Aol + 1e-30f));
            printf("designAol_           = %.2f\n", 290.0f); // from code default
            printf("Acl/designAol_       = %.4f\n", Acl / 290.0f);
        }
    }

    int nMeas = 2000 - measStart;
    double rmsIn = std::sqrt(sumSqIn / nMeas);
    double rmsOut = std::sqrt(sumSqOut / nMeas);
    double gainDB = 20.0 * std::log10(rmsOut / rmsIn);

    printf("\n--- RMS Measurement ---\n");
    printf("RMS In  = %.6e\n", rmsIn);
    printf("RMS Out = %.6e\n", rmsOut);
    printf("Gain    = %.1f dB (expected 29.9 dB)\n", gainDB);
    printf("Offset  = %.1f dB\n", gainDB - 29.9);
    printf("Factor  = %.2fx\n", std::pow(10.0, (gainDB - 29.9) / 20.0));

    return (std::abs(gainDB - 29.9) < 6.0) ? 0 : 1;
}
