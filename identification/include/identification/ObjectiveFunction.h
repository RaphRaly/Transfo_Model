#pragma once

// =============================================================================
// ObjectiveFunction [v3 enriched] -- Multi-component objective for J-A
// parameter identification.
//
// Combines multiple error metrics into a single scalar cost function.
// Each component can be independently weighted to steer the optimizer
// toward physically meaningful solutions.
//
// Components:
//   - MajorLoopError    : RMSE of simulated vs. measured B-H loop [T]
//   - CoercivityError   : |Hc_sim - Hc_meas| / Hc_meas
//   - RemanenceError    : |Br_sim - Br_meas| / Br_meas
//   - THDMatchError     : |THD_sim - THD_meas| / THD_meas
//   - MultiFreqError    : frequency-dependent BH error (dynamic losses)
//   - BHClosurePenalty  : [v3] penalty for non-closing B-H loops
//   - PsychoacousticTHDWeight : [v3] 1/n^2 harmonic weighting
//                          (H2/H3 dominate perception; H7+ negligible)
//
// The objective internally creates a HysteresisModel<LangevinPade>,
// simulates the B-H response for the candidate JAParameterSet, and
// compares against the MeasurementData.
//
// Reference: Szewczyk 2020 (multi-objective J-A identification);
//            Juvela et al. 2024 (Neural DSP PANAMA -- psychoacoustic THD)
// =============================================================================

#include "MeasurementData.h"
#include "../../core/include/core/magnetics/JAParameterSet.h"
#include "../../core/include/core/magnetics/HysteresisModel.h"
#include "../../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../../core/include/core/magnetics/DynamicLosses.h"
#include "../../core/include/core/util/Constants.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <functional>
#include <map>
#include <cassert>

namespace transfo {

// ---------------------------------------------------------------------------
// ObjectiveComponent -- enumeration of all available cost components.
// ---------------------------------------------------------------------------
enum class ObjectiveComponent
{
    MajorLoopError,          // RMSE of full B-H loop
    CoercivityError,         // Relative coercivity error
    RemanenceError,          // Relative remanence error
    THDMatchError,           // THD spectrum matching
    MultiFreqError,          // Multi-frequency B-H error
    BHClosurePenalty,        // [v3] Non-closing loop penalty
    PsychoacousticTHDWeight  // [v3] Perceptually-weighted harmonic error
};

// ---------------------------------------------------------------------------
// ComponentConfig -- weight and configuration for a single component.
// ---------------------------------------------------------------------------
struct ComponentConfig
{
    ObjectiveComponent type;
    float              weight   = 1.0f;    // Multiplicative weight in total cost
    bool               enabled  = true;

    // Component-specific parameters
    float frequencyHz      = 50.0f;   // For MultiFreqError: test frequency
    int   numHarmonics     = 10;      // For THD/Psychoacoustic: max harmonic order
    float closureThreshold = 0.01f;   // For BHClosurePenalty: B tolerance [T]
};

// ---------------------------------------------------------------------------
// ObjectiveFunction -- multi-component cost for J-A identification.
// ---------------------------------------------------------------------------
class ObjectiveFunction
{
public:
    ObjectiveFunction() = default;

    // -----------------------------------------------------------------------
    // setMeasurementData -- set the target measurement to fit against.
    // -----------------------------------------------------------------------
    void setMeasurementData(const MeasurementData& data)
    {
        measData_ = &data;
    }

    // -----------------------------------------------------------------------
    // setSimulationSampleRate -- set sample rate for B-H simulation.
    // Default is 96 kHz for identification accuracy.
    // -----------------------------------------------------------------------
    void setSimulationSampleRate(double sr)
    {
        simSampleRate_ = sr;
    }

    // -----------------------------------------------------------------------
    // setSimulationCycles -- number of full cycles to simulate.
    // First cycle is discarded (transient), loop is extracted from cycle 2+.
    // -----------------------------------------------------------------------
    void setSimulationCycles(int n)
    {
        simCycles_ = std::max(2, n);
    }

    // -----------------------------------------------------------------------
    // addComponent -- add an objective component with weight.
    // -----------------------------------------------------------------------
    void addComponent(ObjectiveComponent type, float weight)
    {
        ComponentConfig cfg;
        cfg.type    = type;
        cfg.weight  = weight;
        cfg.enabled = true;
        components_.push_back(cfg);
    }

    // -----------------------------------------------------------------------
    // addComponent -- add with full configuration.
    // -----------------------------------------------------------------------
    void addComponent(const ComponentConfig& cfg)
    {
        components_.push_back(cfg);
    }

    // -----------------------------------------------------------------------
    // removeComponent -- remove all instances of a component type.
    // -----------------------------------------------------------------------
    void removeComponent(ObjectiveComponent type)
    {
        components_.erase(
            std::remove_if(components_.begin(), components_.end(),
                [type](const ComponentConfig& c) { return c.type == type; }),
            components_.end()
        );
    }

    // -----------------------------------------------------------------------
    // clearComponents -- remove all components.
    // -----------------------------------------------------------------------
    void clearComponents()
    {
        components_.clear();
    }

    // -----------------------------------------------------------------------
    // evaluate -- compute the total cost for a candidate parameter set.
    //
    // This is the function called by optimizers. It:
    //   1. Validates physical constraints (returns penalty if invalid)
    //   2. Simulates a B-H loop with the candidate parameters
    //   3. Computes each enabled component
    //   4. Returns weighted sum
    //
    // Performance: typically ~0.5 ms per evaluation at 96 kHz, 3 cycles.
    // CMA-ES with pop=32 x 500 iter = 16000 evals ~ 8 seconds.
    // -----------------------------------------------------------------------
    float evaluate(const JAParameterSet& params) const
    {
        ++evaluationCount_;

        // Physical validity check
        if (!params.isPhysicallyValid())
            return kInvalidPenalty;

        // Simulate B-H loop
        auto simLoop = simulateBHLoop(params);
        if (simLoop.empty())
            return kInvalidPenalty;

        // Compute each component
        float totalCost = 0.0f;

        for (const auto& comp : components_)
        {
            if (!comp.enabled)
                continue;

            float componentCost = 0.0f;

            switch (comp.type)
            {
            case ObjectiveComponent::MajorLoopError:
                componentCost = computeMajorLoopError(simLoop);
                break;
            case ObjectiveComponent::CoercivityError:
                componentCost = computeCoercivityError(simLoop);
                break;
            case ObjectiveComponent::RemanenceError:
                componentCost = computeRemanenceError(simLoop);
                break;
            case ObjectiveComponent::THDMatchError:
                componentCost = computeTHDMatchError(simLoop, comp.numHarmonics);
                break;
            case ObjectiveComponent::MultiFreqError:
                componentCost = computeMultiFreqError(params, comp.frequencyHz);
                break;
            case ObjectiveComponent::BHClosurePenalty:
                componentCost = computeBHClosurePenalty(simLoop, comp.closureThreshold);
                break;
            case ObjectiveComponent::PsychoacousticTHDWeight:
                componentCost = computePsychoacousticTHD(simLoop, comp.numHarmonics);
                break;
            }

            totalCost += comp.weight * componentCost;
        }

        return totalCost;
    }

    // -----------------------------------------------------------------------
    // getEvaluationCount -- number of evaluate() calls since construction.
    // -----------------------------------------------------------------------
    int getEvaluationCount() const { return evaluationCount_; }

    // -----------------------------------------------------------------------
    // resetEvaluationCount
    // -----------------------------------------------------------------------
    void resetEvaluationCount() { evaluationCount_ = 0; }

    // -----------------------------------------------------------------------
    // getComponents -- access the component list (for reporting).
    // -----------------------------------------------------------------------
    const std::vector<ComponentConfig>& getComponents() const { return components_; }

    // -----------------------------------------------------------------------
    // getLastSimulatedLoop -- retrieve the last simulated B-H loop
    // (useful for visualization during optimization).
    // -----------------------------------------------------------------------
    const std::vector<BHPoint>& getLastSimulatedLoop() const { return lastSimLoop_; }

private:
    const MeasurementData*       measData_ = nullptr;
    std::vector<ComponentConfig> components_;

    // Simulation parameters
    double simSampleRate_ = 96000.0;
    int    simCycles_     = 3;

    // Evaluation counter
    mutable int evaluationCount_ = 0;

    // Cache of last simulated loop (for visualization)
    mutable std::vector<BHPoint> lastSimLoop_;

    // Penalty for physically invalid parameters
    static constexpr float kInvalidPenalty = 1e10f;

    // -----------------------------------------------------------------------
    // simulateBHLoop -- simulate a full B-H cycle using HysteresisModel.
    //
    // Drives the model with a sinusoidal H-field at the measurement frequency
    // (or DC equivalent: slow triangular wave). Extracts the steady-state
    // loop from the last cycle.
    // -----------------------------------------------------------------------
    std::vector<BHPoint> simulateBHLoop(const JAParameterSet& params) const
    {
        if (!measData_)
            return {};

        HysteresisModel<LangevinPade> model;
        model.setParameters(params);
        model.setSampleRate(simSampleRate_);
        model.setMaxIterations(12);
        model.setTolerance(1e-10);
        model.reset();

        double Hmax = measData_->getHmax();
        if (Hmax < 1.0)
            Hmax = 1000.0;   // Fallback

        double freq = measData_->getMetadata().frequencyHz;
        if (freq <= 0.0)
            freq = 1.0;  // DC measurement: use slow 1 Hz equivalent

        int samplesPerCycle = static_cast<int>(simSampleRate_ / freq);
        int totalSamples = samplesPerCycle * simCycles_;

        // Drive with sinusoidal H
        std::vector<BHPoint> fullLoop;
        fullLoop.reserve(static_cast<size_t>(samplesPerCycle));

        for (int n = 0; n < totalSamples; ++n)
        {
            double t = static_cast<double>(n) / simSampleRate_;
            double H = Hmax * std::sin(kTwoPi * freq * t);

            double M = model.solveImplicitStep(H);
            model.commitState();

            double B = kMu0 * (H + M);

            // Store only the last cycle for fitting
            if (n >= samplesPerCycle * (simCycles_ - 1))
                fullLoop.push_back({H, B});
        }

        lastSimLoop_ = fullLoop;
        return fullLoop;
    }

    // -----------------------------------------------------------------------
    // computeMajorLoopError -- RMSE between simulated and measured B-H.
    //
    // For each measured point, find the nearest simulated H value and
    // compare B. Returns RMSE in Tesla.
    // -----------------------------------------------------------------------
    float computeMajorLoopError(const std::vector<BHPoint>& simLoop) const
    {
        if (!measData_ || measData_->empty() || simLoop.empty())
            return kInvalidPenalty;

        return static_cast<float>(measData_->computeRMSE(simLoop));
    }

    // -----------------------------------------------------------------------
    // computeCoercivityError -- relative coercivity error |Hc_sim - Hc_meas|/Hc_meas.
    // -----------------------------------------------------------------------
    float computeCoercivityError(const std::vector<BHPoint>& simLoop) const
    {
        if (!measData_)
            return kInvalidPenalty;

        double HcMeas = measData_->getCoercivity();
        if (HcMeas < 1e-6)
            return 0.0f;  // No coercivity to match

        // Compute Hc from simulated loop
        double HcSim = 0.0;
        for (size_t i = 1; i < simLoop.size(); ++i)
        {
            if (simLoop[i - 1].B * simLoop[i].B < 0.0)
            {
                double frac = -simLoop[i - 1].B / (simLoop[i].B - simLoop[i - 1].B);
                double Hc = simLoop[i - 1].H + frac * (simLoop[i].H - simLoop[i - 1].H);
                HcSim = std::max(HcSim, std::abs(Hc));
            }
        }

        return static_cast<float>(std::abs(HcSim - HcMeas) / HcMeas);
    }

    // -----------------------------------------------------------------------
    // computeRemanenceError -- relative remanence error |Br_sim - Br_meas|/Br_meas.
    // -----------------------------------------------------------------------
    float computeRemanenceError(const std::vector<BHPoint>& simLoop) const
    {
        if (!measData_)
            return kInvalidPenalty;

        double BrMeas = measData_->getRemanence();
        if (BrMeas < 1e-8)
            return 0.0f;

        double BrSim = 0.0;
        for (size_t i = 1; i < simLoop.size(); ++i)
        {
            if (simLoop[i - 1].H * simLoop[i].H < 0.0)
            {
                double frac = -simLoop[i - 1].H / (simLoop[i].H - simLoop[i - 1].H);
                double Br = simLoop[i - 1].B + frac * (simLoop[i].B - simLoop[i - 1].B);
                BrSim = std::max(BrSim, std::abs(Br));
            }
        }

        return static_cast<float>(std::abs(BrSim - BrMeas) / BrMeas);
    }

    // -----------------------------------------------------------------------
    // computeTHDMatchError -- THD spectrum matching.
    //
    // Computes THD of the B waveform (simulated) using a DFT, and compares
    // against THD of the measured B waveform. Returns relative error.
    // -----------------------------------------------------------------------
    float computeTHDMatchError(const std::vector<BHPoint>& simLoop,
                               int numHarmonics) const
    {
        if (!measData_ || simLoop.size() < 16)
            return 0.0f;

        double thdSim  = computeTHD(simLoop, numHarmonics);
        double thdMeas = computeTHDFromMeasurement(numHarmonics);

        if (thdMeas < 1e-6)
            return static_cast<float>(thdSim);  // Penalty for any distortion if none measured

        return static_cast<float>(std::abs(thdSim - thdMeas) / thdMeas);
    }

    // -----------------------------------------------------------------------
    // computeMultiFreqError -- error at a specific frequency.
    //
    // Simulates the J-A model with dynamic losses at the given frequency
    // and compares against measurement data (if multi-frequency data is
    // available). Falls back to static error if only DC data exists.
    // -----------------------------------------------------------------------
    float computeMultiFreqError(const JAParameterSet& params, float freqHz) const
    {
        if (!measData_ || measData_->empty())
            return kInvalidPenalty;

        HysteresisModel<LangevinPade> model;
        model.setParameters(params);
        model.setSampleRate(simSampleRate_);
        model.setMaxIterations(12);
        model.setTolerance(1e-10);
        model.reset();

        DynamicLosses dynLoss;
        dynLoss.setCoefficients(params.K1, params.K2);
        dynLoss.setSampleRate(simSampleRate_);
        dynLoss.reset();

        double Hmax = measData_->getHmax();
        int samplesPerCycle = static_cast<int>(simSampleRate_ / static_cast<double>(freqHz));
        int totalSamples = samplesPerCycle * simCycles_;

        std::vector<BHPoint> simLoop;
        simLoop.reserve(static_cast<size_t>(samplesPerCycle));

        for (int n = 0; n < totalSamples; ++n)
        {
            double t = static_cast<double>(n) / simSampleRate_;
            double H = Hmax * std::sin(kTwoPi * static_cast<double>(freqHz) * t);

            double M = model.solveImplicitStep(H);
            model.commitState();

            double B = kMu0 * (H + M);

            // Add dynamic loss contribution
            double Hdyn = dynLoss.computeHdynamic(B);
            dynLoss.updateState(B);

            // The effective B includes dynamic losses: B_total = B + mu0*Hdyn effect
            // For comparison, we use the total flux density
            if (n >= samplesPerCycle * (simCycles_ - 1))
                simLoop.push_back({H + Hdyn, B});
        }

        return static_cast<float>(measData_->computeRMSE(simLoop));
    }

    // -----------------------------------------------------------------------
    // computeBHClosurePenalty [v3] -- penalty for non-closing loops.
    //
    // A physically correct hysteresis loop must close: B(end) ~ B(start).
    // Unclosed loops indicate numerical issues or unphysical parameters.
    //
    // Returns: (|B_end - B_start| / Bsat)^2 if above threshold, else 0.
    // -----------------------------------------------------------------------
    float computeBHClosurePenalty(const std::vector<BHPoint>& simLoop,
                                  float threshold) const
    {
        if (simLoop.size() < 4)
            return kInvalidPenalty;

        double Bstart = simLoop.front().B;
        double Bend   = simLoop.back().B;
        double gap    = std::abs(Bend - Bstart);

        if (gap < static_cast<double>(threshold))
            return 0.0f;

        // Normalize by Bsat for scale-invariance
        double Bsat = measData_ ? measData_->getBsat() : 1.0;
        if (Bsat < 1e-6) Bsat = 1.0;

        double normalized = gap / Bsat;
        return static_cast<float>(normalized * normalized);
    }

    // -----------------------------------------------------------------------
    // computePsychoacousticTHD [v3] -- 1/n^2 weighted harmonic error.
    //
    // Psychoacoustic weighting: harmonics are weighted by 1/n^2 where n is
    // the harmonic order. This emphasizes H2 and H3 (which dominate the
    // perceived "warmth" of transformer saturation) while de-emphasizing
    // H7+ (which are barely audible).
    //
    //   Weight(n) = 1 / n^2
    //   Cost = Sum over n: Weight(n) * |A_sim(n) - A_meas(n)|^2 / A_meas(1)^2
    //
    // Reference: Juvela et al. 2024 (Neural DSP PANAMA)
    // -----------------------------------------------------------------------
    float computePsychoacousticTHD(const std::vector<BHPoint>& simLoop,
                                    int numHarmonics) const
    {
        if (simLoop.size() < 16 || !measData_)
            return 0.0f;

        // Extract B waveform from simulated loop
        std::vector<double> bSim(simLoop.size());
        for (size_t i = 0; i < simLoop.size(); ++i)
            bSim[i] = simLoop[i].B;

        // Extract B waveform from measurement
        const auto& pts = measData_->getPoints();
        std::vector<double> bMeas(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
            bMeas[i] = pts[i].B;

        // Compute harmonic magnitudes via DFT
        auto harmonicsSim  = computeHarmonicMagnitudes(bSim, numHarmonics);
        auto harmonicsMeas = computeHarmonicMagnitudes(bMeas, numHarmonics);

        // Normalize by fundamental
        double fundSim  = (harmonicsSim.size() > 1)  ? harmonicsSim[1]  : 1.0;
        double fundMeas = (harmonicsMeas.size() > 1) ? harmonicsMeas[1] : 1.0;
        if (fundSim < 1e-12) fundSim = 1e-12;
        if (fundMeas < 1e-12) fundMeas = 1e-12;

        // Psychoacoustic weighted error: 1/n^2 weighting
        double cost = 0.0;
        int maxN = std::min(numHarmonics,
                            std::min(static_cast<int>(harmonicsSim.size()) - 1,
                                     static_cast<int>(harmonicsMeas.size()) - 1));

        for (int n = 2; n <= maxN; ++n)
        {
            double relSim  = harmonicsSim[static_cast<size_t>(n)]  / fundSim;
            double relMeas = harmonicsMeas[static_cast<size_t>(n)] / fundMeas;
            double diff    = relSim - relMeas;

            // 1/n^2 weighting: H2 weight = 0.25, H3 = 0.111, H7 = 0.020
            double weight = 1.0 / (static_cast<double>(n) * static_cast<double>(n));

            cost += weight * diff * diff;
        }

        return static_cast<float>(cost);
    }

    // -----------------------------------------------------------------------
    // THD computation helpers
    // -----------------------------------------------------------------------

    // Compute THD of a B waveform from a BHPoint vector
    double computeTHD(const std::vector<BHPoint>& loop, int numHarmonics) const
    {
        std::vector<double> bWave(loop.size());
        for (size_t i = 0; i < loop.size(); ++i)
            bWave[i] = loop[i].B;

        auto harmonics = computeHarmonicMagnitudes(bWave, numHarmonics);
        if (harmonics.size() < 2 || harmonics[1] < 1e-12)
            return 0.0;

        double sumHarmonicsSq = 0.0;
        for (size_t n = 2; n < harmonics.size(); ++n)
            sumHarmonicsSq += harmonics[n] * harmonics[n];

        return std::sqrt(sumHarmonicsSq) / harmonics[1];
    }

    // Compute THD from measurement data
    double computeTHDFromMeasurement(int numHarmonics) const
    {
        if (!measData_ || measData_->empty())
            return 0.0;

        const auto& pts = measData_->getPoints();
        std::vector<double> bWave(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
            bWave[i] = pts[i].B;

        auto harmonics = computeHarmonicMagnitudes(bWave, numHarmonics);
        if (harmonics.size() < 2 || harmonics[1] < 1e-12)
            return 0.0;

        double sumHarmonicsSq = 0.0;
        for (size_t n = 2; n < harmonics.size(); ++n)
            sumHarmonicsSq += harmonics[n] * harmonics[n];

        return std::sqrt(sumHarmonicsSq) / harmonics[1];
    }

    // -----------------------------------------------------------------------
    // computeHarmonicMagnitudes -- DFT-based harmonic extraction.
    //
    // Computes magnitude of harmonics 0..numHarmonics from a waveform
    // assumed to be exactly one period. Uses Goertzel algorithm for
    // efficiency (only compute the harmonics we need, not full FFT).
    //
    // Returns vector of magnitudes: [DC, fund, H2, H3, ... HN]
    // -----------------------------------------------------------------------
    static std::vector<double> computeHarmonicMagnitudes(const std::vector<double>& signal,
                                                         int numHarmonics)
    {
        int N = static_cast<int>(signal.size());
        if (N < 4)
            return {};

        std::vector<double> magnitudes(static_cast<size_t>(numHarmonics + 1), 0.0);

        for (int k = 0; k <= numHarmonics; ++k)
        {
            // Goertzel algorithm for bin k
            double omega = kTwoPi * static_cast<double>(k) / static_cast<double>(N);
            double coeff = 2.0 * std::cos(omega);
            double s0 = 0.0, s1 = 0.0, s2 = 0.0;

            for (int n = 0; n < N; ++n)
            {
                s0 = signal[static_cast<size_t>(n)] + coeff * s1 - s2;
                s2 = s1;
                s1 = s0;
            }

            // Magnitude = |X(k)| / (N/2)
            double real = s1 - s2 * std::cos(omega);
            double imag = s2 * std::sin(omega);
            double mag = std::sqrt(real * real + imag * imag) / (static_cast<double>(N) * 0.5);

            magnitudes[static_cast<size_t>(k)] = mag;
        }

        return magnitudes;
    }
};

} // namespace transfo
