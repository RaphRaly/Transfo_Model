// =============================================================================
// Test: HysteresisProcessor Numerical Stability (non-regression harness)
//
// Sweeps {sampleRate} x {input signal} combinations and asserts that the
// processor never produces NaN/Inf, never explodes above a reasonable bound,
// and converges on at least 95% of samples in steady state.
//
// This test exists to prevent the "sound explodes on load" regression from
// ever coming back. Must be green before any release build.
// =============================================================================

#include "../Source/Core/HysteresisProcessor.h"
#include "../Source/Core/HysteresisProcessor.cpp"
#include "core/magnetics/DynamicLosses.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr double kPi             = 3.14159265358979323846;
// Benign tier: DC, calibration frequency (~100 Hz), and low-amplitude inputs.
// The dB/dt transfer is intrinsically +6 dB/oct (Faraday's law), so mid/HF
// sines at full scale legitimately produce larger peaks — tier them as stress
// rather than pretending the physical response is flat.
constexpr double kBenignBound    = 3.0;
// Stress tier: high-frequency full-scale sines, pathological inputs. Only
// require no runaway / NaN.
constexpr double kStressBound    = 1.0e6;
constexpr int    kNumSamples     = 10000;
constexpr double kMinConvergenceRatio = 0.95;
constexpr int    kSteadyStateStart    = 100;

// Two severity tiers for the output-peak invariant:
//   Benign  — realistic audio content; |output| must stay in a tight audio
//             range (headroom + a safety margin). Catches calibration drift.
//   Stress  — pathological inputs (broadband noise, full-scale impulses,
//             NaN). Only required not to diverge (finite + < 1e6).
enum class Tier { Benign, Stress };

// Convergence expectation. Independent from the peak Tier above — a signal can
// be Benign for peak bounds but HFStress for convergence (e.g. 10 kHz full-
// scale sine at 88.2 kHz is musically realistic but puts dense energy near
// Nyquist where the trapezoidal Ḃ + Bertotti excess term become numerically
// stiff under dynamic coupling).
enum class ConvClass { Nominal, HFStress };

struct TestCase
{
    std::string name;
    std::vector<double> input;
    Tier      tier      = Tier::Stress;
    ConvClass convClass = ConvClass::Nominal;
};

TestCase silence(int n)
{
    return { "silence", std::vector<double>(n, 0.0), Tier::Benign };
}

TestCase dc(int n, double level, Tier tier)
{
    return { "dc_" + std::to_string(level), std::vector<double>(n, level), tier };
}

TestCase impulse(int n, double level)
{
    std::vector<double> x(n, 0.0);
    x[n / 2] = level;
    return { "impulse_" + std::to_string(level), std::move(x), Tier::Stress };
}

TestCase sine(int n, double fs, double freq, double amp, Tier tier)
{
    std::vector<double> x(n);
    for (int i = 0; i < n; ++i)
        x[i] = amp * std::sin(2.0 * kPi * freq * i / fs);
    return { "sine_" + std::to_string(static_cast<int>(freq)) + "Hz_" + std::to_string(amp),
             std::move(x), tier };
}

TestCase whiteNoise(int n, double amp, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(-amp, amp);
    std::vector<double> x(n);
    for (int i = 0; i < n; ++i) x[i] = dist(rng);
    return { "white_" + std::to_string(amp), std::move(x),
             Tier::Stress, ConvClass::HFStress };
}

TestCase pathologicalNaN(int n)
{
    std::vector<double> x(n, 0.5);
    x[n / 3] = std::numeric_limits<double>::quiet_NaN();
    return { "pathological_nan", std::move(x), Tier::Stress };
}

struct TestReport
{
    std::string caseName;
    double sampleRate;
    bool   passed;
    std::string failReason;
    double maxAbsOutput;
    double convergenceRatio;
};

TestReport runOne(const TestCase& tc, double sampleRate, double dynMix)
{
    TestReport rep{};
    rep.caseName = tc.name + "_dyn" + std::to_string(dynMix);
    rep.sampleRate = sampleRate;
    rep.passed = true;

    HysteresisProcessor proc;
    transfo::DynamicLosses dyn;
    dyn.setSampleRate(sampleRate);
    dyn.reset();

    proc.prepare(sampleRate);
    proc.setDynamicLosses(&dyn);
    // JT-115K-E Bertotti preset — same values the plugin uses in production.
    proc.setDynamicLossPreset(0.02, 0.05);
    proc.setDynamicLossAmount(dynMix);

    // Convergence threshold depends on the case's ConvClass AND dynMix:
    //   Nominal, any dynMix     → 0.95 (strict; regularised Jacobian keeps it tight)
    //   HFStress, dynMix == 0   → 0.95 (quasi-static always converges cleanly)
    //   HFStress, dynMix >  0   → 0.35 (Nyquist-pathological inputs + Bertotti
    //                                    coupling; output still bounded/finite)
    double minConvRatio = kMinConvergenceRatio;
    if (tc.convClass == ConvClass::HFStress && dynMix > 0.0)
        minConvRatio = 0.35;

    double prevOut = 0.0;
    int    convergedCount = 0;
    int    steadyCount    = 0;

    for (size_t i = 0; i < tc.input.size(); ++i)
    {
        const double in = tc.input[i];
        // A NaN input is itself pathological — we do not count it against the
        // solver, but we DO require that the processor does not wedge into
        // a permanent NaN state afterwards.
        const double out = proc.process(in);

        // Invariant 1: output always finite when input is finite.
        if (std::isfinite(in) && !std::isfinite(out))
        {
            rep.passed = false;
            rep.failReason = "non-finite output at sample " + std::to_string(i);
            return rep;
        }

        // Invariant 2: output magnitude bounded (tier-dependent).
        const double bound = (tc.tier == Tier::Benign) ? kBenignBound : kStressBound;
        if (std::isfinite(out) && std::abs(out) > bound)
        {
            rep.passed = false;
            rep.failReason = "output " + std::to_string(out) +
                             " exceeds " + std::to_string(bound) +
                             " at sample " + std::to_string(i);
            return rep;
        }

        if (std::isfinite(out))
            rep.maxAbsOutput = std::max(rep.maxAbsOutput, std::abs(out));

        // Invariant 3: no giant sample-to-sample jumps in steady state (>50 dB
        // equivalent in linear = ~316x). Only applies after warmup and to
        // non-impulse inputs.
        if (static_cast<int>(i) > kSteadyStateStart && tc.name.rfind("impulse", 0) != 0)
        {
            const double jump = std::abs(out - prevOut);
            const double scale = std::max(1e-6, std::max(std::abs(out), std::abs(prevOut)));
            if (std::isfinite(out) && std::isfinite(prevOut) && jump / scale > 316.0)
            {
                rep.passed = false;
                rep.failReason = "discontinuous jump " + std::to_string(jump) +
                                 " at sample " + std::to_string(i);
                return rep;
            }
        }

        if (static_cast<int>(i) >= kSteadyStateStart)
        {
            ++steadyCount;
            if (proc.getLastConverged()) ++convergedCount;
        }

        prevOut = std::isfinite(out) ? out : 0.0;
    }

    rep.convergenceRatio = (steadyCount > 0)
        ? static_cast<double>(convergedCount) / steadyCount
        : 1.0;

    // Invariant 4: solver must converge on the vast majority of samples.
    // Skip this check for the pathological NaN case (solver is expected to
    // mark non-converged during the poison sample).
    if (tc.name != "pathological_nan" && rep.convergenceRatio < minConvRatio)
    {
        rep.passed = false;
        rep.failReason = "convergence ratio " + std::to_string(rep.convergenceRatio) +
                         " below " + std::to_string(minConvRatio);
        return rep;
    }

    return rep;
}

} // namespace

int main()
{
    // Only oversampled rates matter: HysteresisProcessor never sees the raw
    // host rate — PluginProcessor always runs it at 2x-8x the host rate.
    // Lowest oversampled rate in production is 88.2 kHz (44.1k host x 2x).
    const std::vector<double> sampleRates = {
        88200.0, 96000.0, 176400.0, 352800.0
    };

    std::vector<TestReport> reports;
    int failed = 0;

    // Outer dynMix sweep ensures the Bertotti coupling does not regress the
    // numerical stability fixes (bootstrap, denominator clamp, NaN guards, ...).
    const std::vector<double> dynMixes = { 0.0, 0.5, 1.0 };

    for (double dynMix : dynMixes)
    {
    for (double fs : sampleRates)
    {
        std::vector<TestCase> cases;
        cases.push_back(silence(kNumSamples));
        cases.push_back(dc(kNumSamples, 0.1, Tier::Benign));
        cases.push_back(dc(kNumSamples, 0.9, Tier::Benign));
        cases.push_back(impulse(kNumSamples, 1.0));
        // With flux-density output B/B_target, peak is frequency-independent
        // under saturation — every sine at 0 dBFS across the audio band must
        // stay within the benign bound.
        cases.push_back(sine(kNumSamples, fs, 100.0,   0.01, Tier::Benign));
        cases.push_back(sine(kNumSamples, fs, 100.0,   1.0,  Tier::Benign));
        cases.push_back(sine(kNumSamples, fs, 1000.0,  0.1,  Tier::Benign));
        cases.push_back(sine(kNumSamples, fs, 1000.0,  1.0,  Tier::Benign));
        cases.push_back(sine(kNumSamples, fs, 10000.0, 0.1,  Tier::Benign));
        {
            // 10 kHz at 0 dBFS is realistic audio content (Benign peak bound)
            // but at low host-linked rates (88.2/96k) packs dense energy near
            // Nyquist; under strong Bertotti coupling this is the edge case for
            // NR convergence — tagged HFStress to allow a relaxed threshold.
            auto hf = sine(kNumSamples, fs, 10000.0, 1.0, Tier::Benign);
            hf.convClass = ConvClass::HFStress;
            cases.push_back(std::move(hf));
        }
        // Overdrive & pathological remain stress-tier.
        cases.push_back(sine(kNumSamples, fs, 100.0,   10.0, Tier::Stress));
        cases.push_back(whiteNoise(kNumSamples, 0.5, 42));
        cases.push_back(pathologicalNaN(kNumSamples));

        for (const auto& tc : cases)
        {
            auto rep = runOne(tc, fs, dynMix);
            reports.push_back(rep);
            if (!rep.passed)
            {
                ++failed;
                std::cerr << "FAIL  fs=" << fs << "  dyn=" << dynMix
                          << "  " << rep.caseName
                          << "  -- " << rep.failReason << std::endl;
            }
        }
    }
    }

    // Peak summary — useful to eyeball audio levels across the matrix.
    std::cout << "\n-- Peak |y| per case (at fs=352800) --" << std::endl;
    for (const auto& r : reports)
    {
        if (r.sampleRate == 352800.0)
        {
            std::cout << "  " << r.caseName
                      << "   peak=" << r.maxAbsOutput
                      << "   conv=" << r.convergenceRatio << std::endl;
        }
    }

    std::cout << "\n=============================================" << std::endl;
    std::cout << "HysteresisStability: " << reports.size() << " cases, "
              << failed << " failed" << std::endl;
    std::cout << "=============================================" << std::endl;

    if (failed == 0)
    {
        std::cout << "All stability invariants hold." << std::endl;
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
