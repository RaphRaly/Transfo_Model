// Trace Newton internals: y_prev, y0, J, y_new, and committed y12f/v3
// This helps diagnose why TR3 enters cutoff during signal processing.
#include <cmath>
#include <cstdio>
#include "core/preamp/Neve1063Path.h"
#include "core/preamp/GainTable.h"

// We need to add a debug hook to Neve1063Path. Instead, let's just measure
// the output behavior and operating points very densely.
int main()
{
    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amp = 0.001f;

    // Test at position 0 (PASSES) vs position 5 (FAILS)
    for (int pos : {0, 5})
    {
        transfo::Neve1063Path path;
        transfo::Neve1063PathConfig config;
        path.configure(config);
        path.prepare(sampleRate, 512);

        float rfb = transfo::GainTable::getRfb(pos);
        path.setGain(rfb);
        path.reset();

        float beta = 47.0f / (rfb + 47.0f);
        float Acl = 1.0f + rfb / 47.0f;

        printf("=== Position %d: Rfb=%.0f, beta=%.4f, Acl=%.1f (%.1f dB) ===\n",
               pos, rfb, beta, Acl, 20*std::log10(Acl));

        auto op = path.getOperatingPoint();
        printf("  After reset: TR3 Ic=%.4f mA, Aol=%.1f\n\n",
               op.Ic_am3*1000, path.getOpenLoopGain());

        printf("  i   input(mV)  output(V)  TR3_Ic(mA) TR3_Vce  AM1_Ic  AM2_Ic  Aol\n");

        for (int i = 0; i < 30; ++i)
        {
            float t = static_cast<float>(i) / sampleRate;
            float input = amp * std::sin(2.0f * 3.14159265f * freq * t);
            float output = path.processSample(input);

            op = path.getOperatingPoint();
            printf("  %2d  %8.4f  %9.5f  %9.4f  %7.2f  %7.4f  %7.4f  %.1f\n",
                   i, input*1000, output,
                   op.Ic_am3*1000, op.Vce_am3,
                   op.Ic_am1*1000, op.Ic_am2*1000,
                   path.getOpenLoopGain());
        }
        printf("\n");
    }
    return 0;
}
