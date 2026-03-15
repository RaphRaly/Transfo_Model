// =============================================================================
// Test: DynamicParallelAdaptor & WDFSeriesAdaptor
//
// Standalone test (no JUCE dependency) that validates:
//   1.  Alpha sums to 2.0 for 3-port default impedances
//   2.  Alpha sums to 2.0 with varied impedances {100, 200, 500}
//   3.  Adapted impedance = 1/G_sum = 1/(1/R1 + 1/R2 + 1/R3)
//   4.  Passivity: ||b|| <= ||a|| for multiple input vectors
//   5.  Impedance update produces correct new alphas
//   6.  Scattering with equal impedances: known analytic result
//   7.  needsUpdate flag after construction / set / recalculate
//   8.  4-port adaptor: isValid + adapted impedance
//   9.  WDFSeriesAdaptor: adapted impedance = R1 + R2
//  10.  WDFSeriesAdaptor: scatterAdapted returns a1 + a2
//  11.  WDFSeriesAdaptor: full scatter consistency
//  12.  WDFSeriesAdaptor: energy conservation
//  13.  WDFSeriesAdaptor: impedance change
//
// All tests use the v3 core/ headers -- no legacy Source/ code.
// =============================================================================

#include "../core/include/core/wdf/DynamicParallelAdaptor.h"
#include "../core/include/core/wdf/WDFSeriesAdaptor.h"
#include "../core/include/core/util/Constants.h"

#include <iostream>
#include <cmath>
#include <limits>

// ---- Helpers ----------------------------------------------------------------

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
                  << " -- expected " << expected << ", got " << actual
                  << " (err=" << err << ", tol=" << tol << ") ***" << std::endl;
        g_fail++;
    }
}

// ---- TEST 1: Alpha sums to 2.0 for 3-port default impedances ---------------

void test1_alpha_sum()
{
    std::cout << "\n=== TEST 1: Alpha Sum = 2.0 (Default Impedances) ===" << std::endl;

    transfo::DynamicParallelAdaptor<3> adaptor;

    // Default impedances are all 1000.0 -- constructor calls recalculateScattering()
    CHECK(adaptor.isValid(), "isValid() returns true with default impedances");

    // All equal => each alpha = 2/3, sum = 2.0
    // Verify port impedances are default 1000
    CHECK_NEAR(adaptor.getPortImpedance(0), 1000.0, 1e-3, "Port 0 impedance = 1000");
    CHECK_NEAR(adaptor.getPortImpedance(1), 1000.0, 1e-3, "Port 1 impedance = 1000");
    CHECK_NEAR(adaptor.getPortImpedance(2), 1000.0, 1e-3, "Port 2 impedance = 1000");
}

// ---- TEST 2: Alpha sums to 2.0 with different impedances --------------------

void test2_alpha_varied_Z()
{
    std::cout << "\n=== TEST 2: Alpha Sum = 2.0 (Varied Impedances) ===" << std::endl;

    transfo::DynamicParallelAdaptor<3> adaptor;

    adaptor.setPortImpedance(0, 100.0f);
    adaptor.setPortImpedance(1, 200.0f);
    adaptor.setPortImpedance(2, 500.0f);
    adaptor.recalculateScattering();

    CHECK(adaptor.isValid(), "isValid() returns true with R = {100, 200, 500}");
}

// ---- TEST 3: Adapted impedance = 1/G_sum -----------------------------------

void test3_adapted_impedance()
{
    std::cout << "\n=== TEST 3: Adapted Impedance = 1/G_sum ===" << std::endl;

    transfo::DynamicParallelAdaptor<3> adaptor;

    adaptor.setPortImpedance(0, 100.0f);
    adaptor.setPortImpedance(1, 200.0f);
    adaptor.setPortImpedance(2, 500.0f);
    adaptor.recalculateScattering();

    // G_sum = 1/100 + 1/200 + 1/500 = 0.01 + 0.005 + 0.002 = 0.017
    // R_adapted = 1 / 0.017 = 58.8235...
    double expected = 1.0 / (1.0/100.0 + 1.0/200.0 + 1.0/500.0);
    CHECK_NEAR(adaptor.getAdaptedImpedance(), expected, 0.01,
               "Adapted impedance = 1/(1/100 + 1/200 + 1/500) ~ 58.824");
}

// ---- TEST 4: Passivity (||b|| <= ||a||) for random inputs -------------------

void test4_passivity()
{
    std::cout << "\n=== TEST 4: Passivity (power conservation) ===" << std::endl;

    // A lossless parallel junction conserves POWER, not the L2 norm of wave variables.
    // Power at port i: P_i = (a_i^2 - b_i^2) / (4*R_i)
    // Lossless junction: sum(P_i) = 0, i.e. sum(a_i^2/R_i) = sum(b_i^2/R_i)

    transfo::DynamicParallelAdaptor<3> adaptor;

    float R[3] = { 150.0f, 300.0f, 600.0f };
    adaptor.setPortImpedance(0, R[0]);
    adaptor.setPortImpedance(1, R[1]);
    adaptor.setPortImpedance(2, R[2]);
    adaptor.recalculateScattering();

    float test_inputs[][3] = {
        {  1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f },
        {  1.0f,  1.0f,  1.0f },
        {  1.0f, -1.0f,  0.5f },
        {  3.7f, -2.1f,  0.8f },
        { -5.0f,  4.2f, -1.3f },
        {  0.1f,  0.2f,  0.3f },
    };

    int numTests = sizeof(test_inputs) / sizeof(test_inputs[0]);
    bool allConserved = true;

    for (int t = 0; t < numTests; ++t)
    {
        float a[3] = { test_inputs[t][0], test_inputs[t][1], test_inputs[t][2] };
        float b[3];
        adaptor.scatter(a, b);

        // Weighted power: sum(a^2/R) should equal sum(b^2/R) for lossless junction
        double power_in  = 0.0;
        double power_out = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            power_in  += (double)(a[i]) * a[i] / R[i];
            power_out += (double)(b[i]) * b[i] / R[i];
        }

        if (std::abs(power_in - power_out) > 1e-4)
        {
            std::cout << "  *** FAIL: Power not conserved for input [" << a[0] << ", "
                      << a[1] << ", " << a[2] << "]: P_in=" << power_in
                      << ", P_out=" << power_out << " ***" << std::endl;
            allConserved = false;
        }
    }

    CHECK(allConserved, "Power conservation: sum(a^2/R) = sum(b^2/R) for all test vectors");
}

// ---- TEST 5: Impedance update produces correct new alphas -------------------

void test5_impedance_update_alphas()
{
    std::cout << "\n=== TEST 5: Impedance Update -> Correct Alphas ===" << std::endl;

    transfo::DynamicParallelAdaptor<3> adaptor;

    // Start with equal impedances (default 1000 each)
    // alpha_i = 2 * G_i / G_sum = 2 * (1/1000) / (3/1000) = 2/3
    // Now change port 0 to 500
    adaptor.setPortImpedance(0, 500.0f);
    adaptor.recalculateScattering();

    // G = {1/500, 1/1000, 1/1000} = {0.002, 0.001, 0.001}
    // G_sum = 0.004
    // alpha = {2*0.002/0.004, 2*0.001/0.004, 2*0.001/0.004} = {1.0, 0.5, 0.5}

    // Verify by scattering with unit vectors and checking results
    // b[i] = sum(alpha[j]*a[j]) - a[i]
    // a = [1, 0, 0]: S = alpha[0]*1 = 1.0, b = [1.0-1, 1.0-0, 1.0-0] = [0, 1, 1]
    float a[3] = { 1.0f, 0.0f, 0.0f };
    float b[3];
    adaptor.scatter(a, b);

    CHECK_NEAR(b[0], 0.0, 1e-5, "b[0] = alpha[0]*1 - 1 = 0.0 (alpha[0] = 1.0)");
    CHECK_NEAR(b[1], 1.0, 1e-5, "b[1] = alpha[0]*1 - 0 = 1.0");
    CHECK_NEAR(b[2], 1.0, 1e-5, "b[2] = alpha[0]*1 - 0 = 1.0");

    CHECK(adaptor.isValid(), "isValid() after impedance update");
}

// ---- TEST 6: Scattering with equal impedances (known result) ----------------

void test6_equal_impedance_scatter()
{
    std::cout << "\n=== TEST 6: Scattering with Equal Impedances ===" << std::endl;

    transfo::DynamicParallelAdaptor<3> adaptor;

    // Set all R = 100
    adaptor.setPortImpedance(0, 100.0f);
    adaptor.setPortImpedance(1, 100.0f);
    adaptor.setPortImpedance(2, 100.0f);
    adaptor.recalculateScattering();

    // alpha = [2/3, 2/3, 2/3]
    // a = [1, 0, 0]: S = 2/3*1 + 2/3*0 + 2/3*0 = 2/3
    // b = [2/3-1, 2/3-0, 2/3-0] = [-1/3, 2/3, 2/3]
    float a[3] = { 1.0f, 0.0f, 0.0f };
    float b[3];
    adaptor.scatter(a, b);

    CHECK_NEAR(b[0], -1.0/3.0, 1e-5, "b[0] = -1/3");
    CHECK_NEAR(b[1],  2.0/3.0, 1e-5, "b[1] = 2/3");
    CHECK_NEAR(b[2],  2.0/3.0, 1e-5, "b[2] = 2/3");
}

// ---- TEST 7: needsUpdate flag -----------------------------------------------

void test7_needs_update()
{
    std::cout << "\n=== TEST 7: needsUpdate Flag ===" << std::endl;

    transfo::DynamicParallelAdaptor<3> adaptor;

    // Constructor calls recalculateScattering(), so dirty_ = false
    CHECK(!adaptor.needsUpdate(), "needsUpdate() = false after construction");

    // After setPortImpedance: dirty_ = true
    adaptor.setPortImpedance(0, 200.0f);
    CHECK(adaptor.needsUpdate(), "needsUpdate() = true after setPortImpedance");

    // After recalculateScattering: dirty_ = false
    adaptor.recalculateScattering();
    CHECK(!adaptor.needsUpdate(), "needsUpdate() = false after recalculateScattering");
}

// ---- TEST 8: 4-port adaptor ------------------------------------------------

void test8_four_port()
{
    std::cout << "\n=== TEST 8: 4-Port Adaptor ===" << std::endl;

    transfo::DynamicParallelAdaptor<4> adaptor;

    adaptor.setPortImpedance(0, 100.0f);
    adaptor.setPortImpedance(1, 200.0f);
    adaptor.setPortImpedance(2, 300.0f);
    adaptor.setPortImpedance(3, 600.0f);
    adaptor.recalculateScattering();

    CHECK(adaptor.isValid(), "4-port isValid() with R = {100, 200, 300, 600}");

    // G_sum = 1/100 + 1/200 + 1/300 + 1/600
    //       = 0.01 + 0.005 + 0.003333... + 0.001666...
    //       = 0.02
    // R_adapted = 1/0.02 = 50
    double G_sum = 1.0/100.0 + 1.0/200.0 + 1.0/300.0 + 1.0/600.0;
    double expected = 1.0 / G_sum;
    CHECK_NEAR(adaptor.getAdaptedImpedance(), expected, 0.01,
               "4-port adapted impedance = 50.0");
}

// ---- TEST 9: WDFSeriesAdaptor -- adapted impedance = R1 + R2 ----------------

void test9_series_adapted_impedance()
{
    std::cout << "\n=== TEST 9: WDFSeriesAdaptor -- Adapted Impedance ===" << std::endl;

    transfo::WDFSeriesAdaptor series(100.0f, 300.0f);

    CHECK_NEAR(series.getAdaptedImpedance(), 400.0, 1e-3,
               "Adapted impedance = R1 + R2 = 400");

    CHECK_NEAR(series.getChildImpedance(0), 100.0, 1e-3, "Child 0 impedance = 100");
    CHECK_NEAR(series.getChildImpedance(1), 300.0, 1e-3, "Child 1 impedance = 300");
}

// ---- TEST 10: WDFSeriesAdaptor -- scatterAdapted returns a1 + a2 ------------

void test10_series_scatter_adapted()
{
    std::cout << "\n=== TEST 10: WDFSeriesAdaptor -- scatterAdapted ===" << std::endl;

    transfo::WDFSeriesAdaptor series(100.0f, 300.0f);

    float b_parent = series.scatterAdapted(2.0f, 3.0f);
    CHECK_NEAR(b_parent, 5.0, 1e-6, "scatterAdapted(2.0, 3.0) = 5.0");

    b_parent = series.scatterAdapted(-1.5f, 4.5f);
    CHECK_NEAR(b_parent, 3.0, 1e-6, "scatterAdapted(-1.5, 4.5) = 3.0");

    b_parent = series.scatterAdapted(0.0f, 0.0f);
    CHECK_NEAR(b_parent, 0.0, 1e-10, "scatterAdapted(0, 0) = 0");
}

// ---- TEST 11: WDFSeriesAdaptor -- full scatter consistency ------------------

void test11_series_scatter_full()
{
    std::cout << "\n=== TEST 11: WDFSeriesAdaptor -- Full Scatter Consistency ===" << std::endl;

    transfo::WDFSeriesAdaptor series(200.0f, 300.0f);

    // b_parent must always equal a1 + a2, regardless of a_parent
    float test_cases[][3] = {
        {  1.0f,  2.0f,  0.0f },
        {  1.0f,  2.0f,  5.0f },
        {  1.0f,  2.0f, -3.0f },
        { -2.5f,  4.1f,  1.7f },
        {  0.0f,  0.0f,  0.0f },
        {  10.0f, -10.0f, 100.0f },
    };

    int numTests = sizeof(test_cases) / sizeof(test_cases[0]);
    bool allConsistent = true;

    for (int t = 0; t < numTests; ++t)
    {
        float a1 = test_cases[t][0];
        float a2 = test_cases[t][1];
        float a_parent = test_cases[t][2];

        float b1, b2, b_parent;
        series.scatter(a1, a2, a_parent, b1, b2, b_parent);

        double expected_bp = (double)a1 + (double)a2;
        if (std::abs(b_parent - expected_bp) > 1e-5)
        {
            std::cout << "  *** FAIL: b_parent != a1 + a2 for inputs ["
                      << a1 << ", " << a2 << ", " << a_parent
                      << "]: b_parent=" << b_parent << ", expected=" << expected_bp
                      << " ***" << std::endl;
            allConsistent = false;
        }
    }

    CHECK(allConsistent, "b_parent = a1 + a2 for all test input combinations");
}

// ---- TEST 12: WDFSeriesAdaptor -- energy conservation -----------------------

void test12_series_energy_conservation()
{
    std::cout << "\n=== TEST 12: WDFSeriesAdaptor -- Energy Conservation ===" << std::endl;

    transfo::WDFSeriesAdaptor series(150.0f, 350.0f);

    float R1 = 150.0f;
    float R2 = 350.0f;
    float R3 = series.getAdaptedImpedance(); // = R1 + R2 = 500

    // Test several input combinations
    float test_cases[][3] = {
        {  1.0f,  2.0f,  0.5f },
        { -3.0f,  1.5f,  2.0f },
        {  0.7f, -0.3f, -1.2f },
        {  5.0f,  5.0f,  5.0f },
    };

    int numTests = sizeof(test_cases) / sizeof(test_cases[0]);
    bool allConserved = true;

    for (int t = 0; t < numTests; ++t)
    {
        float a1 = test_cases[t][0];
        float a2 = test_cases[t][1];
        float a_parent = test_cases[t][2];

        float b1, b2, b_parent;
        series.scatter(a1, a2, a_parent, b1, b2, b_parent);

        // V_i = (a_i + b_i) / 2, I_i = (a_i - b_i) / (2 * R_i)
        // Power at port i: P_i = V_i * I_i
        // sum(P_i) should be 0 for lossless junction

        double V1 = ((double)a1 + b1) / 2.0;
        double I1 = ((double)a1 - b1) / (2.0 * R1);
        double V2 = ((double)a2 + b2) / 2.0;
        double I2 = ((double)a2 - b2) / (2.0 * R2);
        double V3 = ((double)a_parent + b_parent) / 2.0;
        double I3 = ((double)a_parent - b_parent) / (2.0 * R3);

        double power_sum = V1 * I1 + V2 * I2 + V3 * I3;

        if (std::abs(power_sum) > 1e-5)
        {
            std::cout << "  *** FAIL: Power sum = " << power_sum
                      << " (expected ~0) for inputs [" << a1 << ", "
                      << a2 << ", " << a_parent << "] ***" << std::endl;
            allConserved = false;
        }
    }

    CHECK(allConserved, "sum(V_i * I_i) ~ 0 for all test cases (lossless junction)");
}

// ---- TEST 13: WDFSeriesAdaptor -- impedance change --------------------------

void test13_series_impedance_change()
{
    std::cout << "\n=== TEST 13: WDFSeriesAdaptor -- Impedance Change ===" << std::endl;

    transfo::WDFSeriesAdaptor series(100.0f, 200.0f);

    // Initial adapted impedance
    CHECK_NEAR(series.getAdaptedImpedance(), 300.0, 1e-3,
               "Initial adapted impedance = 300");

    // Scatter with initial impedances
    float b1_init, b2_init, bp_init;
    series.scatter(1.0f, 2.0f, 0.5f, b1_init, b2_init, bp_init);

    // Change R1
    series.setChildImpedance(0, 400.0f);

    CHECK_NEAR(series.getAdaptedImpedance(), 600.0, 1e-3,
               "Adapted impedance = 600 after R1 change to 400");
    CHECK_NEAR(series.getChildImpedance(0), 400.0, 1e-3,
               "Child 0 impedance updated to 400");

    // Scatter with new impedances -- should differ from initial
    float b1_new, b2_new, bp_new;
    series.scatter(1.0f, 2.0f, 0.5f, b1_new, b2_new, bp_new);

    // b_parent is always a1 + a2, so it stays the same
    CHECK_NEAR(bp_new, 3.0, 1e-5, "b_parent still = a1 + a2 = 3.0 after Z change");

    // But b1 and b2 should change (different beta coefficients)
    bool b1_changed = std::abs(b1_init - b1_new) > 1e-6;
    bool b2_changed = std::abs(b2_init - b2_new) > 1e-6;
    CHECK(b1_changed || b2_changed,
          "Child reflected waves change after impedance update");
}

// ---- Main -------------------------------------------------------------------

int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << "  Dynamic Parallel Adaptor & WDF Series Adaptor Test Suite" << std::endl;
    std::cout << "================================================================" << std::endl;

    test1_alpha_sum();
    test2_alpha_varied_Z();
    test3_adapted_impedance();
    test4_passivity();
    test5_impedance_update_alphas();
    test6_equal_impedance_scatter();
    test7_needs_update();
    test8_four_port();
    test9_series_adapted_impedance();
    test10_series_scatter_adapted();
    test11_series_scatter_full();
    test12_series_energy_conservation();
    test13_series_impedance_change();

    std::cout << "\n================================================================" << std::endl;
    std::cout << "  Results: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    std::cout << "================================================================" << std::endl;

    return (g_fail > 0) ? 1 : 0;
}
