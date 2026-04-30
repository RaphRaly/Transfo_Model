// =============================================================================
// Test: Jiles-Atherton canonical derivative convention
//
// Locks the scalar J-A ODE convention used by HysteresisModel:
//   chi = ((1-c)*A + c*D) / (1 - alpha*c*D)
//
// This prevents the previous regression where the chain-rule denominator was
// applied globally to the irreversible branch.
// =============================================================================

#include "test_common.h"
#include "core/magnetics/AnhystereticFunctions.h"
#include "core/magnetics/HysteresisModel.h"
#include "core/magnetics/JAParameterSet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace transfo;

namespace {

double clampDenom(double denom, const JAParameterSet& p)
{
    const double eps = std::max(1e-6, 1e-4 * static_cast<double>(p.k));
    if (std::abs(denom) < eps)
        return std::copysign(eps, denom == 0.0 ? 1.0 : denom);
    return denom;
}

double rawIrreversibleChi(const JAParameterSet& p, double M, double H, int delta)
{
    LangevinPade anhyst;
    const double Heff = H + p.alpha * M;
    const double Man = p.Ms * anhyst.evaluateD(Heff / p.a);
    const double diff = Man - M;

    if (delta * diff <= 0.0)
        return 0.0;

    double chi = diff / clampDenom(delta * p.k - p.alpha * diff, p);
    const double chiMax = 2.0 * static_cast<double>(p.Ms) / p.a;
    return std::clamp(chi, -chiMax, chiMax);
}

double anhystereticSlopeD(const JAParameterSet& p, double M, double H)
{
    LangevinPade anhyst;
    const double Heff = H + p.alpha * M;
    return (p.Ms / p.a) * anhyst.derivativeD(Heff / p.a);
}

std::vector<JAParameterSet> materials()
{
    return {
        JAParameterSet::defaultMuMetal(),
        JAParameterSet::output50NiFe(),
        JAParameterSet::defaultSiFe()
    };
}

void testOriginSusceptibility()
{
    std::printf("\n=== J-A origin susceptibility ===\n");

    for (const auto& p : materials())
    {
        HysteresisModel<LangevinPade> hyst;
        hyst.setParameters(p);

        const double D0 = static_cast<double>(p.Ms) / (3.0 * p.a);
        const double expected = (p.c * D0) / (1.0 - p.alpha * p.c * D0);
        const double actual = hyst.computeRHS(0.0, 0.0, 1);
        const double relErr = std::abs(actual - expected) / std::max(1.0, std::abs(expected));

        std::printf("  Ms=%.3g a=%.3g c=%.3g: chi=%.9g expected=%.9g rel=%.3g\n",
                    p.Ms, p.a, p.c, actual, expected, relErr);
        CHECK(relErr < 0.01, "origin chi matches chain-rule formula");
    }
}

void testAlphaZeroReducesToWeightedBranches()
{
    std::printf("\n=== J-A alpha=0 reduction ===\n");

    JAParameterSet p = JAParameterSet::defaultMuMetal();
    p.alpha = 0.0f;

    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(p);

    const double M = 0.0;
    const double H = 10.0;
    const int delta = 1;
    const double A = rawIrreversibleChi(p, M, H, delta);
    const double D = anhystereticSlopeD(p, M, H);
    const double expected = (1.0 - p.c) * A + p.c * D;
    const double actual = hyst.computeRHS(M, H, delta);

    std::printf("  actual=%.9g expected=%.9g A=%.9g D=%.9g\n",
                actual, expected, A, D);
    CHECK_NEAR(actual, expected, std::abs(expected) * 1e-10 + 1e-10,
               "alpha=0 removes all mean-field denominator feedback");
}

void testCZeroDoesNotApplyGlobalDenominator()
{
    std::printf("\n=== J-A c=0 irreversible branch ===\n");

    JAParameterSet p = JAParameterSet::defaultMuMetal();
    p.c = 0.0f;

    HysteresisModel<LangevinPade> hyst;
    hyst.setParameters(p);

    const double M = 0.0;
    const double H = 1.0;
    const int delta = 1;
    const double expected = rawIrreversibleChi(p, M, H, delta);
    const double actual = hyst.computeRHS(M, H, delta);

    std::printf("  actual=%.9g expected=%.9g\n", actual, expected);
    CHECK_NEAR(actual, expected, std::abs(expected) * 1e-10 + 1e-10,
               "c=0 leaves only A, without 1/(1-alpha*A)");
}

void testJacobianMatchesFiniteDifference()
{
    std::printf("\n=== J-A analytical Jacobian finite-difference check ===\n");

    struct Probe {
        JAParameterSet p;
        double M;
        double H;
        int delta;
    };

    const std::vector<Probe> probes = {
        {JAParameterSet::defaultMuMetal(), 1000.0, 3.0, 1},
        {JAParameterSet::defaultMuMetal(), -1000.0, -3.0, -1},
        {JAParameterSet::output50NiFe(), 2000.0, 12.0, 1},
        {JAParameterSet::defaultSiFe(), 5000.0, 120.0, 1}
    };

    for (const auto& pr : probes)
    {
        HysteresisModel<LangevinPade> hyst;
        hyst.setParameters(pr.p);

        const double eps = std::max(1e-3, std::abs(pr.M) * 1e-6);
        const double fp = hyst.computeRHS(pr.M + eps, pr.H, pr.delta);
        const double fm = hyst.computeRHS(pr.M - eps, pr.H, pr.delta);
        const double fd = (fp - fm) / (2.0 * eps);
        const double analytic = hyst.computeAnalyticalJacobian(pr.M, pr.H, pr.delta);
        const double relErr = std::abs(analytic - fd)
                            / std::max(1.0, std::max(std::abs(analytic), std::abs(fd)));

        std::printf("  H=%8.3f M=%9.3f delta=%2d: analytic=% .9g fd=% .9g rel=%.3g\n",
                    pr.H, pr.M, pr.delta, analytic, fd, relErr);
        CHECK(relErr < 0.02, "analytical Jacobian matches finite difference");
    }
}

} // namespace

int main()
{
    std::printf("J-A Canonical Derivation Tests\n");
    std::printf("==============================\n");

    testOriginSusceptibility();
    testAlphaZeroReducesToWeightedBranches();
    testCZeroDoesNotApplyGlobalDenominator();
    testJacobianMatchesFiniteDifference();

    return test::printSummary("ja_derivation_canonical");
}
