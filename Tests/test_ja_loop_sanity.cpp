// =============================================================================
// Test: Jiles-Atherton loop sanity
//
// Exercises the implicit H-domain solver after the A1 equation change.
// The goal is not datasheet calibration; it checks basic hysteresis invariants:
// finite state, bounded magnetization, non-zero remanence/coercivity, and
// positive loop area for k > 0 and c < 1.
// =============================================================================

#include "test_common.h"
#include "core/magnetics/AnhystereticFunctions.h"
#include "core/magnetics/HysteresisModel.h"
#include "core/magnetics/JAParameterSet.h"
#include "core/util/Constants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace transfo;

namespace {

struct LoopStats {
    bool finite = true;
    double maxAbsM = 0.0;
    double loopArea = 0.0;
    double maxRemanence = 0.0;
    double maxCoercivity = 0.0;
};

LoopStats runLoop(const JAParameterSet& p, double hMax)
{
    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(p);
    hyst.setSampleRate(51200.0);
    hyst.setMaxIterations(12);
    hyst.reset();

    constexpr int samplesPerCycle = 512;
    constexpr int totalCycles = 8;
    constexpr int warmupCycles = 4;
    const int totalSamples = samplesPerCycle * totalCycles;
    const int warmupSamples = samplesPerCycle * warmupCycles;

    LoopStats stats;
    double prevH = 0.0;
    double prevM = 0.0;
    double prevB = 0.0;
    bool havePrev = false;

    for (int n = 0; n < totalSamples; ++n)
    {
        const double phase = kTwoPi * static_cast<double>(n) / samplesPerCycle;
        const double H = hMax * std::sin(phase);
        const double M = hyst.solveImplicitStep(H);
        hyst.commitState();
        const double B = kMu0 * (H + M);

        stats.finite = stats.finite
                    && std::isfinite(H)
                    && std::isfinite(M)
                    && std::isfinite(B)
                    && hyst.getLastConverged();
        stats.maxAbsM = std::max(stats.maxAbsM, std::abs(M));

        if (n >= warmupSamples && havePrev)
        {
            stats.loopArea += 0.5 * (prevH + H) * (B - prevB);

            if ((prevH <= 0.0 && H > 0.0) || (prevH >= 0.0 && H < 0.0))
            {
                const double t = -prevH / (H - prevH);
                const double M_at_H0 = prevM + t * (M - prevM);
                stats.maxRemanence = std::max(stats.maxRemanence, std::abs(M_at_H0));
            }

            if ((prevM <= 0.0 && M > 0.0) || (prevM >= 0.0 && M < 0.0))
            {
                const double t = -prevM / (M - prevM);
                const double H_at_M0 = prevH + t * (H - prevH);
                stats.maxCoercivity = std::max(stats.maxCoercivity, std::abs(H_at_M0));
            }
        }

        prevH = H;
        prevM = M;
        prevB = B;
        havePrev = true;
    }

    stats.loopArea = std::abs(stats.loopArea);
    return stats;
}

void checkMaterial(const char* name, const JAParameterSet& p, double hMax)
{
    std::printf("\n=== J-A loop sanity: %s ===\n", name);
    const LoopStats s = runLoop(p, hMax);

    std::printf("  max|M|=%.9g  area=%.9g  rem=%.9g  Hc=%.9g\n",
                s.maxAbsM, s.loopArea, s.maxRemanence, s.maxCoercivity);

    CHECK(s.finite, "loop state remains finite and converged");
    CHECK(s.maxAbsM <= 1.1001 * static_cast<double>(p.Ms),
          "magnetization stays inside solver safety bound");
    CHECK(s.loopArea > 1e-6, "loop area is positive");
    CHECK(s.maxRemanence > 1e-3, "remanence is non-zero");
    CHECK(s.maxCoercivity > 1e-3, "coercivity is non-zero");
}

} // namespace

int main()
{
    std::printf("J-A Loop Sanity Tests\n");
    std::printf("=====================\n");

    checkMaterial("mu-metal", JAParameterSet::defaultMuMetal(), 250.0);
    checkMaterial("50NiFe", JAParameterSet::output50NiFe(), 400.0);
    checkMaterial("SiFe", JAParameterSet::defaultSiFe(), 900.0);

    return test::printSummary("ja_loop_sanity");
}
