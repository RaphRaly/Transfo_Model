// =============================================================================
// test_harrison_micpre.cpp — Numerical verification of the Harrison Mic Pre
//
// Checks:
//   1. DC (z=1): Gain = 1.0 exactly (evaluated in double to avoid cancellation)
//   2. Mid-band, alpha=0: Gain matches TRUE analog magnitude at 500 Hz / 1 kHz
//      (NOT the asymptotic 1+Rf/Rg which only holds for f << 1591 Hz)
//   3. Mid-band, alpha=1: Gain ~ 1.004 (nearly flat, pole effect negligible)
//   4. HF (f>10kHz): Gain tends to 1.0
//   5. Stability: all poles inside unit circle at standard sample rates
//   6. Coefficient symmetry: b1_z == a1_z
//   7. BalancedInput scaling: A_term, PAD, phase
//   8. Full-chain gain consistency
//   9. Impulse response stability
//  10. Gain monotonicity with alpha
//  11. True asymptotic mid-band (at 50 Hz, well below pole)
// =============================================================================

#include "../core/include/core/harrison/ComponentValues.h"
#include "../core/include/core/harrison/OpAmpGainStage.h"
#include "../core/include/core/harrison/HarrisonMicPre.h"

#include <cmath>
#include <cstdio>
#include <cassert>
#include <vector>

using namespace Harrison::MicPre;

static constexpr double PI_D = 3.14159265358979323846;
static constexpr float  PI_F = 3.14159265358979f;

// ─── Helpers ────────────────────────────────────────────────────────────────

// Compute magnitude response using DOUBLE precision to avoid cancellation near DC.
// Reads coefficients from OpAmpGainStage (which now returns double).
static double biquadMagnitude(const OpAmpGainStage& stage, double freqHz, double sampleRate)
{
    const double omega = 2.0 * PI_D * freqHz / sampleRate;
    const double cosw  = std::cos(omega);
    const double sinw  = std::sin(omega);
    const double cos2w = std::cos(2.0 * omega);
    const double sin2w = std::sin(2.0 * omega);

    const double b0 = stage.getB0();
    const double b1 = stage.getB1();
    const double b2 = stage.getB2();
    const double a1 = stage.getA1();
    const double a2 = stage.getA2();

    const double numReal = b0 + b1 * cosw + b2 * cos2w;
    const double numImag = -(b1 * sinw + b2 * sin2w);
    const double denReal = 1.0 + a1 * cosw + a2 * cos2w;
    const double denImag = -(a1 * sinw + a2 * sin2w);

    const double numMag2 = numReal * numReal + numImag * numImag;
    const double denMag2 = denReal * denReal + denImag * denImag;

    return std::sqrt(numMag2 / denMag2);
}

// Compute exact analog magnitude |H(j*2*pi*f)| for reference comparison.
static double analogMagnitude(double freqHz, double alpha)
{
    const double dRf = static_cast<double>(Rf);
    const double dRm = static_cast<double>(Rm);
    const double dCc = static_cast<double>(Cc);
    const double dC  = static_cast<double>(C);
    const double Rg  = alpha * static_cast<double>(R109) + static_cast<double>(R108);
    const double dG  = 1.0 / dRf + 1.0 / dRm;

    const double A_s  = dCc * dC * (dG * Rg + 1.0);
    const double Bn_s = dCc * dG + dC * (Rg + dRf) / (dRm * dRf);
    const double Bd_s = dCc * dG + dC * Rg / (dRf * dRm);
    const double D_s  = 1.0 / (dRf * dRm);

    const double w = 2.0 * PI_D * freqHz;
    const double w2 = w * w;

    // H(jw) = (A*(jw)^2 + Bn*(jw) + D) / (A*(jw)^2 + Bd*(jw) + D)
    //       = (-A*w^2 + D + j*Bn*w) / (-A*w^2 + D + j*Bd*w)
    const double realPart = -A_s * w2 + D_s;
    const double numR = realPart, numI = Bn_s * w;
    const double denR = realPart, denI = Bd_s * w;

    const double numMag2 = numR * numR + numI * numI;
    const double denMag2 = denR * denR + denI * denI;

    return std::sqrt(numMag2 / denMag2);
}

// Check if biquad poles are inside unit circle (DOUBLE precision).
static bool polesStable(const OpAmpGainStage& stage)
{
    const double a1 = stage.getA1();
    const double a2 = stage.getA2();

    // Poles of z^2 + a1*z + a2 = 0
    const double disc = a1 * a1 - 4.0 * a2;
    if (disc >= 0.0) {
        const double sqrtDisc = std::sqrt(disc);
        const double z1 = (-a1 + sqrtDisc) / 2.0;
        const double z2 = (-a1 - sqrtDisc) / 2.0;
        return (std::abs(z1) < 1.0) && (std::abs(z2) < 1.0);
    } else {
        // Complex conjugate: |z|^2 = a2 (must be positive for complex roots)
        return a2 >= 0.0 ? (a2 < 1.0) : false;
    }
}

// Stub transformer for full-chain test.
// Matches TransformerModel contract: processBlock is unity-gain normalized,
// caller (HarrisonMicPre) applies getTurnsRatio() separately.
struct StubTransformer
{
    float turnsRatio = 10.0f;
    void processBlock(const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i) out[i] = in[i];  // unity-gain (core model)
    }
    float getTurnsRatio() const { return turnsRatio; }
};

static int testCount = 0;
static int passCount = 0;

static void check(bool condition, const char* name, const char* detail = "")
{
    testCount++;
    if (condition) { passCount++; std::printf("  [PASS] %s %s\n", name, detail); }
    else           {              std::printf("  [FAIL] %s %s\n", name, detail); }
}

static void checkApprox(double actual, double expected, double tol, const char* name)
{
    char detail[160];
    std::snprintf(detail, sizeof(detail),
                  "(actual=%.8f, expected=%.8f, tol=%.1e)", actual, expected, tol);
    check(std::abs(actual - expected) < tol, name, detail);
}

// ===== TEST 1: DC Gain = 1.0 (evaluated at z=1, i.e. f ~ 0) =====
static void testDCGain()
{
    std::printf("\n--- Test 1: DC Gain ---\n");

    for (float sr : { 48000.0f, 96000.0f, 192000.0f }) {
        OpAmpGainStage stage;
        stage.prepare(sr);

        for (float alpha : { 0.0f, 0.5f, 1.0f }) {
            stage.updateCoefficients(alpha);

            // Evaluate H(z=1) = (b0+b1+b2) / (1+a1+a2) in double
            const double b_sum = stage.getB0() + stage.getB1() + stage.getB2();
            const double a_sum = 1.0 + stage.getA1() + stage.getA2();
            const double dcGain = b_sum / a_sum;

            char name[80];
            std::snprintf(name, sizeof(name),
                          "DC gain @ sr=%.0f, alpha=%.1f", sr, alpha);
            checkApprox(dcGain, 1.0, 1e-6, name);
        }
    }
}

// ===== TEST 2: Gain at 500 Hz / 1 kHz matches analog reference (alpha=0) =====
static void testGainVsAnalog()
{
    std::printf("\n--- Test 2: Digital vs Analog Magnitude (alpha=0) ---\n");
    const double sr = 48000.0;
    OpAmpGainStage stage;
    stage.prepare(static_cast<float>(sr));
    stage.updateCoefficients(0.0f);

    for (double freq : { 50.0, 100.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) {
        const double digital = biquadMagnitude(stage, freq, sr);
        const double analog  = analogMagnitude(freq, 0.0);

        char name[80];
        std::snprintf(name, sizeof(name), "Digital vs Analog @ %.0f Hz", freq);
        // BLT with pre-warp should match analog within 0.5% across audio band
        checkApprox(digital, analog, analog * 0.005, name);
    }
}

// ===== TEST 3: True mid-band gain at low frequency (50 Hz, f << 1591 Hz) =====
static void testTrueMidBand()
{
    std::printf("\n--- Test 3: True Mid-band (50 Hz, well below pole) ---\n");
    const double sr = 48000.0;
    OpAmpGainStage stage;
    stage.prepare(static_cast<float>(sr));

    // alpha = 0 (max gain): 1 + 100/220 = 1.4545
    stage.updateCoefficients(0.0f);
    double g0 = biquadMagnitude(stage, 50.0, sr);
    checkApprox(g0, 1.0 + 100.0/220.0, 0.01, "Mid-band MAX @ 50 Hz");

    // alpha = 1 (min gain): 1 + 100/25220 = 1.00397
    stage.updateCoefficients(1.0f);
    double g1 = biquadMagnitude(stage, 50.0, sr);
    checkApprox(g1, 1.0 + 100.0/25220.0, 0.001, "Mid-band MIN @ 50 Hz");
}

// ===== TEST 4: HF Gain -> 1.0 =====
static void testHFGain()
{
    std::printf("\n--- Test 4: HF Gain -> 1.0 ---\n");
    const double sr = 48000.0;
    OpAmpGainStage stage;
    stage.prepare(static_cast<float>(sr));
    stage.updateCoefficients(0.0f);

    double gain10k = biquadMagnitude(stage, 10000.0, sr);
    double gain20k = biquadMagnitude(stage, 20000.0, sr);

    check(gain10k < 1.15, "HF gain < 1.15 @ 10kHz");
    check(gain20k < 1.05, "HF gain < 1.05 @ 20kHz");
    check(gain10k > 0.9,  "HF gain > 0.9  @ 10kHz (no spurious attenuation)");
}

// ===== TEST 5: Pole Stability at All Standard Sample Rates =====
static void testStability()
{
    std::printf("\n--- Test 5: Pole Stability ---\n");
    const float sampleRates[] = { 44100.0f, 48000.0f, 88200.0f,
                                   96000.0f, 176400.0f, 192000.0f };
    OpAmpGainStage stage;

    for (float sr : sampleRates) {
        stage.prepare(sr);
        for (float alpha : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
            stage.updateCoefficients(alpha);
            char name[64];
            std::snprintf(name, sizeof(name),
                          "Stable @ %.0f Hz, alpha=%.2f", sr, alpha);
            check(polesStable(stage), name);
        }
    }
}

// ===== TEST 6: Coefficient property b1_z == a1_z =====
static void testCoefficientSymmetry()
{
    std::printf("\n--- Test 6: Coefficient Symmetry (b1 == a1) ---\n");
    OpAmpGainStage stage;
    stage.prepare(48000.0f);

    for (float alpha : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        stage.updateCoefficients(alpha);
        char name[64];
        std::snprintf(name, sizeof(name), "b1 == a1 @ alpha=%.2f", alpha);
        checkApprox(stage.getB1(), stage.getA1(), 1e-12, name);
    }
}

// ===== TEST 7: getMidBandGain consistency =====
static void testGetMidBandGain()
{
    std::printf("\n--- Test 7: getMidBandGain Consistency ---\n");
    OpAmpGainStage stage;
    stage.prepare(48000.0f);

    double gMax = static_cast<double>(stage.getMidBandGain(0.0f));
    double gMin = static_cast<double>(stage.getMidBandGain(1.0f));

    checkApprox(gMax, 1.0 + 100.0/220.0,   1e-4, "getMidBandGain(0) = 1.4545");
    checkApprox(gMin, 1.0 + 100.0/25220.0, 1e-4, "getMidBandGain(1) = 1.004");
}

// ===== TEST 8: BalancedInput Scaling =====
static void testBalancedInputScaling()
{
    std::printf("\n--- Test 8: BalancedInput Scaling ---\n");

    double A_term = static_cast<double>(R100) / (static_cast<double>(R100) + 150.0 * 0.5);
    checkApprox(A_term, 0.98909, 0.001, "A_term (Z_s=150)");
    checkApprox(static_cast<double>(PAD_ATTENUATION), 0.10526, 0.0001, "PAD attenuation");

    double expectedRbias = (36000.0 * 75000.0) / (36000.0 + 75000.0);
    checkApprox(static_cast<double>(R_BIAS), expectedRbias, 1.0, "R_BIAS = R110 || R111");
}

// ===== TEST 9: Full-chain with stub transformer =====
static void testFullChain()
{
    std::printf("\n--- Test 9: Full Chain (Stub Transformer) ---\n");

    StubTransformer transfo;
    transfo.turnsRatio = 10.0f;

    HarrisonMicPre<StubTransformer> micPre;
    micPre.setTransformer(&transfo);
    micPre.prepareToPlay(48000.0f, 512);

    micPre.setMicGain(1.0f);  // alpha = 0 -> max gain
    micPre.setPadEnabled(false);
    micPre.setPhaseReverse(false);
    micPre.setSourceImpedance(150.0f);

    // Let smoothing settle (100ms)
    std::vector<float> warmup(4800, 0.0f);
    micPre.processBlock(warmup.data(), static_cast<int>(warmup.size()));

    // 500 Hz test tone, 50ms
    const int testLen = 2400;
    std::vector<float> testSignal(testLen);
    for (int i = 0; i < testLen; ++i)
        testSignal[i] = 0.1f * std::sin(2.0f * PI_F * 500.0f * static_cast<float>(i) / 48000.0f);

    std::vector<float> original = testSignal;
    micPre.processBlock(testSignal.data(), testLen);

    // RMS of last 1000 samples (steady state)
    double rmsOut = 0.0, rmsIn = 0.0;
    for (int i = testLen - 1000; i < testLen; ++i) {
        rmsOut += static_cast<double>(testSignal[i]) * testSignal[i];
        rmsIn  += static_cast<double>(original[i]) * original[i];
    }
    rmsOut = std::sqrt(rmsOut / 1000.0);
    rmsIn  = std::sqrt(rmsIn / 1000.0);

    const double measuredGain = rmsOut / rmsIn;

    // Expected: A_term * N * H_analog(500Hz, alpha=0)
    const double A_term = static_cast<double>(R100)
                        / (static_cast<double>(R100) + 150.0 * 0.5);
    const double H_500 = analogMagnitude(500.0, 0.0);
    const double expectedGain = A_term * 10.0 * H_500;

    char detail[128];
    std::snprintf(detail, sizeof(detail), "(measured=%.4f, expected=%.4f)",
                  measuredGain, expectedGain);
    check(std::abs(measuredGain - expectedGain) / expectedGain < 0.02,
          "Full-chain gain within 2%", detail);
}

// ===== TEST 10: Impulse response settles (no divergence) =====
static void testImpulseResponse()
{
    std::printf("\n--- Test 10: Impulse Response Settles ---\n");

    OpAmpGainStage stage;
    stage.prepare(48000.0f);
    stage.updateCoefficients(0.0f);

    float y = stage.processSample(1.0f);
    float maxVal = std::abs(y);

    for (int i = 0; i < 1000; ++i) {
        y = stage.processSample(0.0f);
        maxVal = std::max(maxVal, std::abs(y));
    }

    check(std::abs(y) < 0.001f, "Impulse response decays to < 0.001");
    check(maxVal < 10.0f,       "No divergence (peak < 10)");
}

// ===== TEST 11: Gain Monotonic with Alpha =====
static void testGainMonotonicity()
{
    std::printf("\n--- Test 11: Gain Monotonic with Alpha ---\n");
    OpAmpGainStage stage;
    stage.prepare(48000.0f);

    double prevGain = 999.0;
    bool monotonic = true;

    for (int i = 0; i <= 100; ++i) {
        float alpha = static_cast<float>(i) / 100.0f;
        stage.updateCoefficients(alpha);
        double gain = biquadMagnitude(stage, 500.0, 48000.0);

        if (gain > prevGain + 1e-10) {
            monotonic = false;
            std::printf("    Monotonicity break at alpha=%.2f: "
                        "gain=%.8f > prev=%.8f\n", alpha, gain, prevGain);
        }
        prevGain = gain;
    }
    check(monotonic, "Gain strictly decreasing with alpha @ 500 Hz");
}

// ===== TEST 12: Analog reference self-consistency =====
static void testAnalogReference()
{
    std::printf("\n--- Test 12: Analog Reference Self-check ---\n");

    // DC: analog gain must be exactly 1.0
    // Use f=1e-5 Hz (well below lowest pole at ~0.032 Hz for alpha=1)
    checkApprox(analogMagnitude(1e-5, 0.0), 1.0, 1e-6, "Analog DC (alpha=0)");
    checkApprox(analogMagnitude(1e-5, 1.0), 1.0, 1e-6, "Analog DC (alpha=1)");

    // Low freq: must approach 1+Rf/Rg
    // NOTE: actual H(s) lower pole is at ~3.7 Hz (alpha=0) or ~0.032 Hz (alpha=1),
    // NOT 0.033 Hz for all alpha — the pole shifts with Rg. Use 50 Hz to be safe.
    double g_a0 = analogMagnitude(50.0, 0.0);
    checkApprox(g_a0, 1.0 + 100.0/220.0, 0.005, "Analog 50Hz (alpha=0) ~ 1.4545");

    // At 500 Hz: known values from cross-validation
    checkApprox(analogMagnitude(500.0, 0.0), 1.3879, 0.001, "Analog 500Hz (alpha=0) ~ 1.388");
    checkApprox(analogMagnitude(1000.0, 0.0), 1.2687, 0.001, "Analog 1kHz (alpha=0) ~ 1.269");
}

int main()
{
    std::printf("========================================\n");
    std::printf("Harrison Mic Pre — Numerical Verification\n");
    std::printf("  (all coefficient/magnitude math in double)\n");
    std::printf("========================================\n");

    testDCGain();
    testGainVsAnalog();
    testTrueMidBand();
    testHFGain();
    testStability();
    testCoefficientSymmetry();
    testGetMidBandGain();
    testBalancedInputScaling();
    testFullChain();
    testImpulseResponse();
    testGainMonotonicity();
    testAnalogReference();

    std::printf("\n========================================\n");
    std::printf("Results: %d / %d passed\n", passCount, testCount);
    std::printf("========================================\n");

    return (passCount == testCount) ? 0 : 1;
}
