// Minimal diagnostic: trace TR3 operating point through prepare/setGain/reset/signal
#include <cmath>
#include <cstdio>
#include "core/preamp/Neve1063Path.h"
#include "core/preamp/GainTable.h"

int main()
{
    transfo::Neve1063Path path;
    transfo::Neve1063PathConfig config;
    path.configure(config);

    printf("=== TR3 Trace Diagnostic (position 5, Rfb=1430) ===\n\n");

    // Step 1: After prepare
    path.prepare(96000.0f, 512);
    auto op = path.getOperatingPoint();
    printf("[After prepare] TR3: Vce=%.3f Ic=%.4f mA  Aol=%.1f\n",
           op.Vce_am3, op.Ic_am3*1000, path.getOpenLoopGain());

    // Step 2: After setGain
    float rfb = transfo::GainTable::getRfb(5);
    path.setGain(rfb);
    op = path.getOperatingPoint();
    printf("[After setGain] TR3: Vce=%.3f Ic=%.4f mA  Aol=%.1f\n",
           op.Vce_am3, op.Ic_am3*1000, path.getOpenLoopGain());

    // Step 3: After reset
    path.reset();
    op = path.getOperatingPoint();
    printf("[After reset]   TR3: Vce=%.3f Ic=%.4f mA  Aol=%.1f\n\n",
           op.Vce_am3, op.Ic_am3*1000, path.getOpenLoopGain());

    // Step 4: Process tone samples and trace TR3
    const float sampleRate = 96000.0f;
    const float freq = 1000.0f;
    const float amp = 0.001f;

    printf("Sample   Input(mV)   Output(V)    TR3_Vce   TR3_Ic(mA)   Aol\n");
    printf("------   ---------   ---------    -------   ----------   ---\n");

    for (int i = 0; i < 200; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        float input = amp * std::sin(2.0f * 3.14159265f * freq * t);
        float output = path.processSample(input);

        // Print every 10 samples for the first 200
        if (i % 10 == 0 || i < 5)
        {
            op = path.getOperatingPoint();
            printf("%5d    %8.4f    %9.4f    %7.3f   %10.4f   %.1f\n",
                   i, input*1000, output, op.Vce_am3, op.Ic_am3*1000,
                   path.getOpenLoopGain());
        }
    }

    return 0;
}
