// =============================================================================
// Test: CPWL Passivity & JAParameterSet Validation
//
// Standalone test (no JUCE dependency) that validates:
//   1. CPWLLeaf::assertPassivity() — slope bounds enforcement
//   2. CPWLLeaf::assertWDFeasibility() — port Z range feasibility
//   3. JAParameterSet::isPhysicallyValid() — stability condition k > alpha*Ms
//   4. JAParameterSet::clampToValid() — auto-correction
//   5. JAParameterSet log-space round-trip (toLogSpace -> fromLogSpace)
//   6. Material family bounds coherence
//   7. AnhystereticFunctions — LangevinPade limits and derivative
//   8. HysteresisModel — commit/rollback double-buffering
//
// All tests use the v3 core/ headers — no legacy Source/ code.
// =============================================================================

#include "../core/include/core/magnetics/CPWLLeaf.h"
#include "../core/include/core/magnetics/JAParameterSet.h"
#include "../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../core/include/core/magnetics/HysteresisModel.h"
#include "../core/include/core/magnetics/DynamicLosses.h"
#include "../core/include/core/util/Constants.h"

#include <iostream>
#include <cmath>
#include <limits>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

void CHECK(bool cond, const char* msg)
{
    if (cond)
    {
        std::cout << "  PASS: " << msg << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "  *** FAIL: " << msg << " ***" << std::endl;
        g_fail++;
    }
}

void CHECK_NEAR(double actual, double expected, double tol, const char* msg)
{
    double err = std::abs(actual - expected);
    if (err <= tol)
    {
        std::cout << "  PASS: " << msg << " (err=" << err << ")" << std::endl;
        g_pass++;
    }
    else
    {
        std::cout << "  *** FAIL: " << msg
                  << " — expected " << expected << ", got " << actual
                  << " (err=" << err << ", tol=" << tol << ") ***" << std::endl;
        g_fail++;
    }
}

// ─── TEST 1: CPWLLeaf::assertPassivity() ────────────────────────────────────

void test1_passivity_valid()
{
    std::cout << "\n=== TEST 1: CPWLLeaf Passivity — Valid Slopes ===" << std::endl;

    transfo::CPWLLeaf leaf;
    leaf.setPassivityBounds(0.001f, 1000.0f);

    // Segments with slopes within bounds
    transfo::CPWLSegmentCoeffs segs[3];
    segs[0] = { 0.01f, 0.0f, -1e6f, 0.0f, 0.0f };
    segs[1] = { 1.0f, 0.0f, -1.0f, 0.0f, 0.0f };
    segs[2] = { 0.5f, 0.5f, 1.0f, 0.0f, 0.0f };

    leaf.setAscendingSegments(segs, 3);
    leaf.setDescendingSegments(segs, 3);

    CHECK(leaf.assertPassivity(), "Valid slopes pass passivity check");
}

void test1b_passivity_invalid()
{
    std::cout << "\n=== TEST 1b: CPWLLeaf Passivity — Invalid Slopes ===" << std::endl;

    transfo::CPWLLeaf leaf;
    leaf.setPassivityBounds(0.01f, 100.0f);

    // Segment with slope outside bounds (too steep)
    transfo::CPWLSegmentCoeffs segs[2];
    segs[0] = { 0.5f, 0.0f, -1e6f, 0.0f, 0.0f };
    segs[1] = { 200.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // slope=200 > m_max=100

    leaf.setAscendingSegments(segs, 2);
    leaf.setDescendingSegments(segs, 1); // descending OK

    CHECK(!leaf.assertPassivity(), "Slope > m_max detected as passivity violation");

    // Segment with negative slope (below m_min)
    transfo::CPWLSegmentCoeffs negSegs[1];
    negSegs[0] = { -0.5f, 0.0f, -1e6f, 0.0f, 0.0f }; // slope < 0 < m_min

    transfo::CPWLLeaf leaf2;
    leaf2.setPassivityBounds(0.01f, 100.0f);
    leaf2.setAscendingSegments(negSegs, 1);
    leaf2.setDescendingSegments(negSegs, 1);

    CHECK(!leaf2.assertPassivity(), "Negative slope detected as passivity violation");
}

// ─── TEST 2: CPWLLeaf::assertWDFeasibility() ───────────────────────────────

void test2_wdf_feasibility()
{
    std::cout << "\n=== TEST 2: WDF Feasibility (Z Range Check) ===" << std::endl;

    transfo::CPWLLeaf leaf;

    // Segments with slopes that produce Z in [0.01, 100]
    // Z = 1/|slope|
    // slope=0.1 -> Z=10 (in range)
    // slope=1.0 -> Z=1 (in range)
    transfo::CPWLSegmentCoeffs segs[2];
    segs[0] = { 0.1f, 0.0f, -1e6f, 0.0f, 0.0f };
    segs[1] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    leaf.setAscendingSegments(segs, 2);
    leaf.setDescendingSegments(segs, 2);

    CHECK(leaf.assertWDFeasibility(0.01f, 100.0f), "Slopes produce Z in feasible range");

    // Slope=0.001 -> Z=1000, outside [0.01, 100]
    transfo::CPWLSegmentCoeffs badSegs[1];
    badSegs[0] = { 0.001f, 0.0f, -1e6f, 0.0f, 0.0f }; // Z = 1000

    transfo::CPWLLeaf leaf2;
    leaf2.setAscendingSegments(badSegs, 1);
    leaf2.setDescendingSegments(badSegs, 1);

    CHECK(!leaf2.assertWDFeasibility(0.01f, 100.0f), "Z=1000 outside feasible range detected");
}

// ─── TEST 3: JAParameterSet::isPhysicallyValid() ───────────────────────────

void test3_ja_validity()
{
    std::cout << "\n=== TEST 3: JAParameterSet Physical Validity ===" << std::endl;

    // Valid preset: default Mu-Metal
    auto muMetal = transfo::JAParameterSet::defaultMuMetal();
    CHECK(muMetal.isPhysicallyValid(), "Default Mu-Metal preset is valid");

    // Valid preset: default NiFe50
    auto nife50 = transfo::JAParameterSet::defaultNiFe50();
    CHECK(nife50.isPhysicallyValid(), "Default NiFe50 preset is valid");

    // Valid preset: default SiFe
    auto sife = transfo::JAParameterSet::defaultSiFe();
    CHECK(sife.isPhysicallyValid(), "Default SiFe preset is valid");

    // Invalid: k <= alpha * Ms (stability violation)
    transfo::JAParameterSet unstable;
    unstable.Ms = 1e6f;
    unstable.alpha = 1e-3f;
    unstable.k = 500.0f; // k=500 <= alpha*Ms = 1000 UNSTABLE
    unstable.a = 80.0f;
    unstable.c = 0.5f;
    CHECK(!unstable.isPhysicallyValid(), "k <= alpha*Ms detected as unstable");

    // Invalid: negative Ms
    transfo::JAParameterSet negMs;
    negMs.Ms = -1e5f;
    CHECK(!negMs.isPhysicallyValid(), "Negative Ms detected as invalid");

    // Invalid: c > 1
    transfo::JAParameterSet badC;
    badC = transfo::JAParameterSet::defaultMuMetal();
    badC.c = 1.5f;
    CHECK(!badC.isPhysicallyValid(), "c > 1 detected as invalid");

    // Invalid: c < 0
    transfo::JAParameterSet negC;
    negC = transfo::JAParameterSet::defaultMuMetal();
    negC.c = -0.1f;
    CHECK(!negC.isPhysicallyValid(), "c < 0 detected as invalid");

    // Invalid: negative K1
    transfo::JAParameterSet negK1;
    negK1 = transfo::JAParameterSet::defaultMuMetal();
    negK1.K1 = -0.01f;
    CHECK(!negK1.isPhysicallyValid(), "Negative K1 detected as invalid");
}

// ─── TEST 4: JAParameterSet::clampToValid() ─────────────────────────────────

void test4_clamp_to_valid()
{
    std::cout << "\n=== TEST 4: JAParameterSet clampToValid ===" << std::endl;

    // Start with unstable parameters
    transfo::JAParameterSet bad;
    bad.Ms = 1e6f;
    bad.alpha = 1e-3f;
    bad.k = 50.0f; // k=50 < alpha*Ms=1000 UNSTABLE
    bad.a = 80.0f;
    bad.c = 0.5f;
    bad.K1 = -0.1f; // Negative (invalid)
    bad.K2 = 0.0f;

    CHECK(!bad.isPhysicallyValid(), "Input is indeed invalid");

    auto fixed = bad.clampToValid();
    CHECK(fixed.isPhysicallyValid(), "clampToValid produces valid parameters");
    CHECK(fixed.k > fixed.alpha * fixed.Ms, "Stability condition enforced: k > alpha*Ms");
    CHECK(fixed.K1 >= 0.0f, "K1 clamped to non-negative");

    // Clamp on already valid parameters should be identity
    auto muMetal = transfo::JAParameterSet::defaultMuMetal();
    auto clamped = muMetal.clampToValid();
    CHECK_NEAR(clamped.Ms, muMetal.Ms, 1.0f, "clampToValid is identity on valid params (Ms)");
    CHECK_NEAR(clamped.k, muMetal.k, 1.0f, "clampToValid is identity on valid params (k)");
}

// ─── TEST 5: Log-space round-trip ───────────────────────────────────────────

void test5_log_space_roundtrip()
{
    std::cout << "\n=== TEST 5: JAParameterSet Log-Space Round-Trip ===" << std::endl;

    auto original = transfo::JAParameterSet::defaultMuMetal();
    auto logParams = original.toLogSpace();
    auto recovered = transfo::JAParameterSet::fromLogSpace(logParams);

    CHECK_NEAR(recovered.Ms, original.Ms, original.Ms * 0.01, "Ms round-trip (1% tol)");
    CHECK_NEAR(recovered.a, original.a, original.a * 0.01, "a round-trip (1% tol)");
    CHECK_NEAR(recovered.k, original.k, original.k * 0.01, "k round-trip (1% tol)");
    CHECK_NEAR(recovered.alpha, original.alpha, original.alpha * 0.1, "alpha round-trip (10% tol)");
    CHECK_NEAR(recovered.c, original.c, 0.02, "c round-trip (abs tol 0.02)");
}

// ─── TEST 6: Material family bounds coherence ───────────────────────────────

void test6_material_bounds()
{
    std::cout << "\n=== TEST 6: Material Family Bounds Coherence ===" << std::endl;

    auto families = {
        transfo::MaterialFamily::MuMetal_80NiFe,
        transfo::MaterialFamily::NiFe_50,
        transfo::MaterialFamily::GO_SiFe,
        transfo::MaterialFamily::Custom
    };

    const char* names[] = { "MuMetal", "NiFe50", "GO-SiFe", "Custom" };
    int idx = 0;

    for (auto family : families)
    {
        auto bounds = transfo::JAParameterSet::getDefaultBounds(family);

        // min < max for all parameters
        bool coherent = (bounds.Ms_min < bounds.Ms_max) &&
                        (bounds.a_min < bounds.a_max) &&
                        (bounds.k_min < bounds.k_max) &&
                        (bounds.alpha_min < bounds.alpha_max) &&
                        (bounds.c_min < bounds.c_max);

        std::string msg = std::string(names[idx]) + " bounds: min < max";
        CHECK(coherent, msg.c_str());

        // All bounds positive
        bool positive = (bounds.Ms_min > 0) && (bounds.a_min > 0) &&
                        (bounds.k_min > 0) && (bounds.alpha_min > 0) &&
                        (bounds.c_min > 0);

        msg = std::string(names[idx]) + " bounds: all positive";
        CHECK(positive, msg.c_str());

        // c max <= 1 (reversibility ratio)
        msg = std::string(names[idx]) + " bounds: c_max <= 1";
        CHECK(bounds.c_max <= 1.0f, msg.c_str());

        idx++;
    }
}

// ─── TEST 7: LangevinPade — limits and derivative ───────────────────────────

void test7_langevin_pade()
{
    std::cout << "\n=== TEST 7: LangevinPade Function Properties ===" << std::endl;

    transfo::LangevinPade L;

    // L(0) = 0 (odd function, zero at origin)
    CHECK_NEAR(L.evaluate(0.0f), 0.0f, 1e-6, "L(0) = 0");

    // L'(0) = 1/3 (Taylor expansion)
    CHECK_NEAR(L.derivative(0.0f), 1.0f / 3.0f, 1e-4, "L'(0) = 1/3");

    // L(x) -> 1 as x -> +inf
    CHECK_NEAR(L.evaluate(100.0f), 1.0f, 1e-4, "L(+inf) -> 1");

    // L(x) -> -1 as x -> -inf
    CHECK_NEAR(L.evaluate(-100.0f), -1.0f, 1e-4, "L(-inf) -> -1");

    // Odd symmetry: L(-x) = -L(x)
    float x = 2.5f;
    CHECK_NEAR(L.evaluate(-x), -L.evaluate(x), 1e-5, "L(-x) = -L(x) (odd symmetry)");

    // Monotonically increasing: L(x1) < L(x2) for x1 < x2
    bool monotone = true;
    float prev_val = L.evaluate(-5.0f);
    for (float xi = -4.5f; xi <= 5.0f; xi += 0.5f)
    {
        float val = L.evaluate(xi);
        if (val < prev_val - 1e-6f)
        {
            monotone = false;
            break;
        }
        prev_val = val;
    }
    CHECK(monotone, "L(x) is monotonically increasing");

    // Derivative always non-negative
    bool derivNonNeg = true;
    for (float xi = -8.0f; xi <= 8.0f; xi += 0.25f)
    {
        if (L.derivative(xi) < -1e-6f)
        {
            derivNonNeg = false;
            break;
        }
    }
    CHECK(derivNonNeg, "L'(x) >= 0 everywhere");

    // Double-precision version at moderate x: compare to exact coth(x) - 1/x
    double xd = 2.0;
    double exact = 1.0 / std::tanh(xd) - 1.0 / xd;
    double approx = L.evaluateD(xd);
    CHECK_NEAR(approx, exact, 1e-10, "Double-precision L(2.0) matches exact");
}

// ─── TEST 8: HysteresisModel commit/rollback ────────────────────────────────

void test8_commit_rollback()
{
    std::cout << "\n=== TEST 8: HysteresisModel Commit/Rollback ===" << std::endl;

    transfo::HysteresisModel<transfo::LangevinPade> model;
    model.setParameters(transfo::JAParameterSet::defaultMuMetal());
    model.setSampleRate(44100.0);
    model.reset();

    // Initial state: M = 0
    CHECK_NEAR(model.getMagnetization(), 0.0, 1e-10, "Initial M = 0");

    // Solve a step: H = 100 A/m
    double M1 = model.solveImplicitStep(100.0);
    CHECK(std::isfinite(M1), "M1 is finite after first solve");
    CHECK(M1 != 0.0, "M1 != 0 (magnetization changed)");

    // Tentative state changed, committed not yet
    CHECK_NEAR(model.getTentativeMagnetization(), M1, 1e-10, "Tentative M = M1");
    CHECK_NEAR(model.getMagnetization(), 0.0, 1e-10, "Committed M still = 0");

    // Rollback: tentative goes back to committed
    model.rollbackState();
    CHECK_NEAR(model.getTentativeMagnetization(), 0.0, 1e-10, "After rollback: tentative = 0");

    // Solve again and commit
    double M2 = model.solveImplicitStep(100.0);
    model.commitState();
    CHECK_NEAR(model.getMagnetization(), M2, 1e-10, "After commit: committed M = M2");

    // Solve next step
    double M3 = model.solveImplicitStep(200.0);
    CHECK(std::abs(M3) > std::abs(M2) - 1e-6, "Higher H -> higher |M| (or equal)");
    model.commitState();

    // Verify convergence
    CHECK(model.getLastConverged(), "Solver converged");
    CHECK(model.getLastIterationCount() > 0, "Iteration count > 0");
    CHECK(model.getLastIterationCount() <= 8, "Iteration count <= maxIter (8)");
}

// ─── TEST 9: DynamicLosses ──────────────────────────────────────────────────

void test9_dynamic_losses()
{
    std::cout << "\n=== TEST 9: DynamicLosses (Eddy + Excess) ===" << std::endl;

    transfo::DynamicLosses losses;
    losses.setCoefficients(0.01f, 0.005f); // K1, K2
    losses.setSampleRate(44100.0);
    losses.reset();

    // At rest (B constant): no dynamic loss
    losses.updateState(1.0);
    double H_dyn = losses.computeHdynamic(1.0);
    CHECK_NEAR(H_dyn, 0.0, 1e-10, "No dynamic loss when B is constant");

    // Increasing B: positive dB/dt -> positive H_dyn
    losses.updateState(1.0);
    H_dyn = losses.computeHdynamic(1.1);
    CHECK(H_dyn > 0.0, "Positive dB/dt -> positive H_dyn");

    // Decreasing B: negative dB/dt -> negative H_dyn
    losses.updateState(1.1);
    H_dyn = losses.computeHdynamic(0.9);
    CHECK(H_dyn < 0.0, "Negative dB/dt -> negative H_dyn");

    // Higher dB/dt -> higher |H_dyn|
    losses.updateState(1.0);
    double H_small = std::abs(losses.computeHdynamic(1.01));
    losses.updateState(1.0);
    double H_large = std::abs(losses.computeHdynamic(1.1));
    CHECK(H_large > H_small, "Higher |dB/dt| -> higher |H_dyn|");

    // Zero coefficients -> zero loss
    transfo::DynamicLosses noLoss;
    noLoss.setCoefficients(0.0f, 0.0f);
    noLoss.setSampleRate(44100.0);
    noLoss.reset();
    noLoss.updateState(1.0);
    CHECK_NEAR(noLoss.computeHdynamic(2.0), 0.0, 1e-15, "Zero K1,K2 -> zero dynamic loss");
}

// ─── TEST 10: CPWLAnhysteretic ──────────────────────────────────────────────

void test10_cpwl_anhysteretic()
{
    std::cout << "\n=== TEST 10: CPWLAnhysteretic Function ===" << std::endl;

    transfo::CPWLAnhysteretic cpwl;
    cpwl.setBreakpoint(2.0f, 0.333f, 0.01f);

    // Inner region: slope = 0.333
    CHECK_NEAR(cpwl.evaluate(0.0f), 0.0f, 1e-6, "CPWL(0) = 0");
    CHECK_NEAR(cpwl.evaluate(1.0f), 0.333f, 1e-3, "CPWL(1) = 0.333 (inner slope)");

    // Derivative in inner region
    CHECK_NEAR(cpwl.derivative(0.5f), 0.333f, 1e-3, "CPWL'(0.5) = 0.333");

    // Outer region: slope = 0.01
    CHECK_NEAR(cpwl.derivative(3.0f), 0.01f, 1e-3, "CPWL'(3.0) = 0.01 (outer)");

    // Odd symmetry
    CHECK_NEAR(cpwl.evaluate(-1.5f), -cpwl.evaluate(1.5f), 1e-6, "CPWL odd symmetry");
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << "  CPWL Passivity & JAParameterSet Test Suite (v3 core/)" << std::endl;
    std::cout << "================================================================" << std::endl;

    test1_passivity_valid();
    test1b_passivity_invalid();
    test2_wdf_feasibility();
    test3_ja_validity();
    test4_clamp_to_valid();
    test5_log_space_roundtrip();
    test6_material_bounds();
    test7_langevin_pade();
    test8_commit_rollback();
    test9_dynamic_losses();
    test10_cpwl_anhysteretic();

    std::cout << "\n================================================================" << std::endl;
    std::cout << "  Results: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    std::cout << "================================================================" << std::endl;

    return (g_fail > 0) ? 1 : 0;
}
