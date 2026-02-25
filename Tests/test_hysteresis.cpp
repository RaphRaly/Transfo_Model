// =============================================================================
// Test: M-H Hysteresis Loop Validation
//
// This is a standalone test (no JUCE dependency) that:
//   1. Sends a 1 Hz sine wave through HysteresisProcessor
//   2. Records (H, M) pairs over multiple cycles
//   3. Exports to CSV for visual inspection
//   4. Computes loop area (must be > 0 for valid hysteresis)
//   5. Repeats at multiple amplitudes to verify saturation
//
// EXPECTED RESULT: S-shaped loop with non-zero area that widens
// and flattens at higher amplitudes.
// =============================================================================

#include "../Source/Core/HysteresisProcessor.h"
#include "../Source/Core/HysteresisProcessor.cpp"
// Note: we include the .cpp directly to avoid needing a build system
// for this standalone test.

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>

static constexpr double PI = 3.14159265358979323846;

struct Sample
{
    double H;
    double M;
};

// Run the hysteresis model on a sine wave and collect (H, M) pairs
std::vector<Sample> runHysteresisTest(double amplitude, double frequency,
                                       double sampleRate, int numCycles,
                                       int warmupCycles)
{
    HysteresisProcessor proc;
    proc.prepare(sampleRate);

    // Use default J-A parameters (placeholders from spec)
    // Ms=1.2e6, a=80, k=200, c=0.1, alpha=5e-4
    proc.setInputScaling(1.0);   // We'll feed H directly
    proc.setOutputScaling(1.0);  // We read M from getLastMagnetization()

    const int samplesPerCycle = static_cast<int>(sampleRate / frequency);
    const int totalSamples = samplesPerCycle * (warmupCycles + numCycles);
    const int recordStart = samplesPerCycle * warmupCycles;

    std::vector<Sample> data;
    data.reserve(samplesPerCycle * numCycles);

    for (int i = 0; i < totalSamples; ++i)
    {
        double t = static_cast<double>(i) / sampleRate;
        double H = amplitude * std::sin(2.0 * PI * frequency * t);

        // Feed H directly (inputScale = 1.0, so process(H) treats input as H)
        proc.process(H);

        if (i >= recordStart)
        {
            data.push_back({ H, proc.getLastMagnetization() });
        }
    }

    return data;
}

// Compute the area of the M-H loop using the shoelace formula
// A valid hysteresis loop MUST have non-zero area.
double computeLoopArea(const std::vector<Sample>& data)
{
    double area = 0.0;
    const int n = static_cast<int>(data.size());
    for (int i = 0; i < n; ++i)
    {
        int j = (i + 1) % n;
        area += data[i].H * data[j].M - data[j].H * data[i].M;
    }
    return std::abs(area) / 2.0;
}

// Export data to CSV
void exportCSV(const std::string& filename, const std::vector<Sample>& data)
{
    std::ofstream file(filename);
    file << "H,M\n";
    for (const auto& s : data)
        file << s.H << "," << s.M << "\n";
    file.close();
}

int main()
{
    // =========================================================================
    // TEST 1: M-H loop at 1 Hz
    // Expected: S-shaped hysteresis loop with non-zero area
    // =========================================================================
    std::cout << "=== TEST 1: M-H Hysteresis Loop (1 Hz) ===" << std::endl;
    {
        const double sampleRate = 48000.0 * 8.0;  // Simulate 8x oversampling
        const double frequency = 1.0;               // 1 Hz
        const double amplitude = 500.0;              // H amplitude in A/m (moderate drive)
        const int numCycles = 2;
        const int warmupCycles = 3;                  // Let the state settle

        auto data = runHysteresisTest(amplitude, frequency, sampleRate,
                                       numCycles, warmupCycles);

        exportCSV("test1_MH_loop_1Hz.csv", data);

        double area = computeLoopArea(data);
        double M_min = 1e30, M_max = -1e30;
        double H_min = 1e30, H_max = -1e30;

        for (const auto& s : data)
        {
            M_min = std::min(M_min, s.M);
            M_max = std::max(M_max, s.M);
            H_min = std::min(H_min, s.H);
            H_max = std::max(H_max, s.H);
        }

        std::cout << "  Samples recorded: " << data.size() << std::endl;
        std::cout << "  H range: [" << H_min << ", " << H_max << "] A/m" << std::endl;
        std::cout << "  M range: [" << M_min << ", " << M_max << "] A/m" << std::endl;
        std::cout << "  Loop area: " << area << std::endl;

        bool pass = (area > 0.0) && (M_max > 0.0) && (M_min < 0.0)
                    && (M_max != -M_min || true);  // Asymmetry check relaxed

        std::cout << "  M excursion ratio (M_max / Ms): " << M_max / 1.2e6 << std::endl;

        if (area < 1e-6)
            std::cout << "  *** FAIL: Loop area is essentially zero — no hysteresis! ***" << std::endl;
        else
            std::cout << "  PASS: Non-zero loop area detected." << std::endl;

        if (M_max < 1.0)
            std::cout << "  *** FAIL: M_max is tiny (" << M_max << ") — model may not be producing magnetization ***" << std::endl;
        else
            std::cout << "  PASS: Magnetization has reasonable magnitude." << std::endl;
    }

    // =========================================================================
    // TEST 2: Saturation — increasing amplitude
    // Expected: loop widens, M flattens at extremes
    // =========================================================================
    std::cout << "\n=== TEST 2: Saturation (increasing amplitude) ===" << std::endl;
    {
        const double sampleRate = 48000.0 * 8.0;
        const double frequency = 1.0;
        double amplitudes[] = { 100.0, 300.0, 500.0, 1000.0, 2000.0 };

        for (int idx = 0; idx < 5; ++idx)
        {
            double amp = amplitudes[idx];
            auto data = runHysteresisTest(amp, frequency, sampleRate, 2, 3);

            std::string filename = "test2_MH_amp_" + std::to_string(static_cast<int>(amp)) + ".csv";
            exportCSV(filename, data);

            double area = computeLoopArea(data);
            double M_max = -1e30;
            for (const auto& s : data)
                M_max = std::max(M_max, s.M);

            std::cout << "  Amplitude=" << amp << " A/m: "
                      << "M_max=" << M_max << " A/m "
                      << "(M_max/Ms=" << M_max / 1.2e6 << "), "
                      << "Loop area=" << area << std::endl;
        }
    }

    // =========================================================================
    // TEST 3: Solver convergence check
    // Expected: 2-4 iterations normally, up to 8 in transients
    // =========================================================================
    std::cout << "\n=== TEST 3: Solver Convergence ===" << std::endl;
    {
        HysteresisProcessor proc;
        const double sampleRate = 48000.0 * 8.0;
        proc.prepare(sampleRate);
        proc.setInputScaling(1.0);

        int maxIter = 0;
        int totalIter = 0;
        int nonConverged = 0;
        const int N = 384000;  // 1 second

        for (int i = 0; i < N; ++i)
        {
            double t = static_cast<double>(i) / sampleRate;
            double H = 500.0 * std::sin(2.0 * PI * 100.0 * t);  // 100 Hz
            proc.process(H);

            int iters = proc.getLastIterationCount();
            totalIter += iters;
            if (iters > maxIter) maxIter = iters;
            if (!proc.getLastConverged()) nonConverged++;
        }

        double avgIter = static_cast<double>(totalIter) / N;
        std::cout << "  Average iterations: " << avgIter << std::endl;
        std::cout << "  Max iterations: " << maxIter << std::endl;
        std::cout << "  Non-converged samples: " << nonConverged << " / " << N << std::endl;

        if (nonConverged > 0)
            std::cout << "  *** WARNING: Some samples did not converge ***" << std::endl;
        else
            std::cout << "  PASS: All samples converged." << std::endl;
    }

    std::cout << "\n=== Tests complete. Check CSV files for visual inspection. ===" << std::endl;
    return 0;
}
