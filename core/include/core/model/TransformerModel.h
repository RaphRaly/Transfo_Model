#pragma once

// =============================================================================
// TransformerModel — Top-level audio transformer simulation engine.
//
// Orchestrates all components: HSIMSolver, OversamplingEngine, ToleranceModel.
// This is the class that the plugin layer calls for audio processing.
//
// Two processing modes [v3]:
//   Physical : J-A complete + OS 4x — for bounce/render offline
//   Realtime : CPWL directional + ADAA, NO OS — for monitoring
//
// [v3] ProcessingMode revised: ADAA replaces OS 2x in Realtime.
// CPU saving: 30-50% vs v2.
//
// Template on NonlinearLeaf allows:
//   TransformerModel<CPWLLeaf>           → Realtime mode
//   TransformerModel<JilesAthertonLeaf>  → Physical mode
//
// Runtime switch via std::variant in the plugin layer.
// =============================================================================

#include "../dsp/OversamplingEngine.h"
#include "../magnetics/AnhystereticFunctions.h"
#include "../magnetics/CPWLLeaf.h"
#include "../magnetics/JilesAthertonLeaf.h"
#include "../util/Constants.h"
#include "../util/SPSCQueue.h"
#include "../util/SmoothedValue.h"
#include "../wdf/HSIMSolver.h"
#include "ToleranceModel.h"
#include "TransformerConfig.h"
#include <cmath>
#include <type_traits>

namespace transfo {

enum class ProcessingMode {
  Physical, // J-A complete, OS 4x — for bounce/render offline
  Realtime  // CPWL directional + ADAA, NO OS — monitoring [v3]
};

struct BHSample {
  float h = 0.0f;
  float b = 0.0f;
};

template <typename NonlinearLeaf> class TransformerModel {
public:
  TransformerModel() = default;

  // ─── Configuration ──────────────────────────────────────────────────────
  void setConfig(const TransformerConfig &config) {
    config_ = config;
    configureCircuit();
  }

  void setProcessingMode(ProcessingMode mode) { processingMode_ = mode; }

  // ─── Prepare ────────────────────────────────────────────────────────────
  void prepareToPlay(float sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    if (processingMode_ == ProcessingMode::Physical) {
      // Physical: OS 4x polyphase filter design
      oversampler_.prepare(sampleRate, maxBlockSize, kOversamplingPhysical);
      hsim_.prepareToPlay(oversampler_.getOversampledRate(),
                          maxBlockSize * kOversamplingPhysical);
    } else {
      // Realtime: no OS, ADAA coefficients precomputed in CPWLLeaf
      hsim_.prepareToPlay(sampleRate, maxBlockSize);
    }

    // Smoothed parameters
    inputGain_.reset(sampleRate, 0.02);
    outputGain_.reset(sampleRate, 0.02);
    mix_.reset(sampleRate, 0.02);
    mix_.setCurrentAndTargetValue(1.0f);

    bhQueue_.reset();
    bhDownsampleCounter_ = 0;

    configureCircuit();
  }

  // ─── Process Block ──────────────────────────────────────────────────────
  void processBlock(const float *input, float *output, int numSamples) {
    if (processingMode_ == ProcessingMode::Realtime) {
      // [v3] Realtime: ADAA in CPWLLeaf, NO oversampling
      processBlockRealtime(input, output, numSamples);
    } else {
      // Physical: OS 4x
      processBlockPhysical(input, output, numSamples);
    }
  }

  // ─── Parameter control ──────────────────────────────────────────────────
  void setInputGain(float dB) { inputGain_.setTargetValue(dBtoLinear(dB)); }
  void setOutputGain(float dB) { outputGain_.setTargetValue(dBtoLinear(dB)); }
  void setMix(float mix01) { mix_.setTargetValue(mix01); }

  // ─── Monitoring ─────────────────────────────────────────────────────────
  struct MonitorData {
    int lastIterCount = 0;
    bool lastConverged = true;
    float spectralRadius = 0.0f;
    int convergenceFailures = 0;
  };

  MonitorData getMonitorData() const {
    MonitorData data;
    data.lastIterCount = hsim_.getLastIterationCount();
    data.lastConverged = hsim_.getLastConverged();
    data.convergenceFailures = hsim_.getConvergenceGuard().getFailureCount();
#ifndef NDEBUG
    data.spectralRadius = hsim_.getDiagnostics().getLastSpectralRadius();
#endif
    return data;
  }

  size_t readBHSamples(BHSample *dest, size_t maxSamples) {
    size_t count = 0;
    while (count < maxSamples && bhQueue_.pop(dest[count])) {
      count++;
    }
    return count;
  }

  // ─── Access ─────────────────────────────────────────────────────────────
  HSIMSolver<NonlinearLeaf> &getHSIM() { return hsim_; }
  const TransformerConfig &getConfig() const { return config_; }
  ProcessingMode getProcessingMode() const { return processingMode_; }

  void reset() {
    hsim_.reset();
    oversampler_.reset();
    bhQueue_.reset();
    directHyst_.reset();
  }

private:
  TransformerConfig config_;
  HSIMSolver<NonlinearLeaf> hsim_;
  OversamplingEngine oversampler_;
  ProcessingMode processingMode_ = ProcessingMode::Realtime;

  SmoothedValue<float> inputGain_{1.0f};
  SmoothedValue<float> outputGain_{1.0f};
  SmoothedValue<float> mix_{1.0f};

  float sampleRate_ = kDefaultSampleRate;
  int maxBlockSize_ = 512;

  SPSCQueue<BHSample, 2048> bhQueue_;
  int bhDownsampleCounter_ = 0;

  // ─── Direct J-A processing (bypasses broken HSIM topology) ──────────────
  HysteresisModel<LangevinPade> directHyst_;
  float hScale_ = 100.0f;  // Input → H field scaling
  float bNorm_ = 1.0f;     // B → output normalization (unity gain in linear region)

  // ─── Realtime processing — direct J-A bypass (no OS) ─────────────────────
  void processBlockRealtime(const float *input, float *output, int numSamples) {
    for (int k = 0; k < numSamples; ++k) {
      const float gain_in = inputGain_.getNextValue();
      const float gain_out = outputGain_.getNextValue();
      const float mixVal = mix_.getNextValue();

      const float dry = input[k];
      const float x = input[k] * gain_in;

      // Direct J-A: scale to H field, solve hysteresis, compute B
      const float H = x * hScale_;
      const double M = directHyst_.solveImplicitStep(static_cast<double>(H));
      directHyst_.commitState();

      const float B = kMu0f * (H + static_cast<float>(M));
      const float wet = B * bNorm_;

      // BH scope data
      if (++bhDownsampleCounter_ >= 32) {
        bhDownsampleCounter_ = 0;
        bhQueue_.push(BHSample{H, B});
      }

      output[k] = (dry * (1.0f - mixVal) + wet * mixVal) * gain_out;
    }
  }

  // ─── Physical processing — direct J-A bypass + OS 4x ─────────────────────
  void processBlockPhysical(const float *input, float *output, int numSamples) {
    float *osBuffer = oversampler_.getOversampledBuffer();
    const int osSize = oversampler_.getOversampledSize(numSamples);

    // Upsample
    oversampler_.upsample(input, numSamples, osBuffer);

    // Process at oversampled rate with direct J-A
    for (int k = 0; k < osSize; ++k) {
      const float x = osBuffer[k] * inputGain_.getCurrentValue();

      // Direct J-A: scale to H field, solve hysteresis, compute B
      const float H = x * hScale_;
      const double M = directHyst_.solveImplicitStep(static_cast<double>(H));
      directHyst_.commitState();

      const float B = kMu0f * (H + static_cast<float>(M));
      osBuffer[k] = B * bNorm_;

      // BH scope data (less frequent for oversampled)
      if (++bhDownsampleCounter_ >= 128) {
        bhDownsampleCounter_ = 0;
        bhQueue_.push(BHSample{H, B});
      }
    }

    // Downsample
    oversampler_.downsample(osBuffer, osSize, output);

    // Apply output gain and mix
    for (int k = 0; k < numSamples; ++k) {
      const float gain_out = outputGain_.getNextValue();
      const float mixVal = mix_.getNextValue();
      output[k] = (input[k] * (1.0f - mixVal) + output[k] * mixVal) * gain_out;
    }
  }

  // ─── Configure the WDF circuit from TransformerConfig ───────────────────
  void configureCircuit() {
    // Configure nonlinear leaves (kept for future HSIM topology fix)
    for (int i = 0; i < hsim_.getNumNonlinearLeaves(); ++i) {
      auto &leaf = hsim_.getNonlinearLeaf(i);

      if constexpr (std::is_same_v<NonlinearLeaf,
                                   JilesAthertonLeaf<LangevinPade>> ||
                    std::is_same_v<NonlinearLeaf,
                                   JilesAthertonLeaf<CPWLAnhysteretic>>) {
        leaf.configure(config_.core.effectiveLength(),
                       config_.core.effectiveArea(), config_.material,
                       sampleRate_);
      }
    }

    // Configure ME junctions with turns ratio (kept for future HSIM topology fix)
    for (int i = 0; i < hsim_.getNumMEJunctions(); ++i) {
      int turns = (i == 0) ? config_.windings.turnsRatio_N1
                           : config_.windings.turnsRatio_N2;
      hsim_.getMEJunction(i).configure(turns, sampleRate_);
    }

    // ─── Direct J-A model (bypasses HSIM) ──────────────────────────────────
    float procRate = (processingMode_ == ProcessingMode::Physical)
                         ? sampleRate_ * kOversamplingPhysical
                         : sampleRate_;
    if (procRate <= 0.0f) procRate = kDefaultSampleRate;

    directHyst_.setParameters(config_.material);
    directHyst_.setSampleRate(static_cast<double>(procRate));
    directHyst_.reset();

    // Scaling: map ±1.0 digital audio to H field around saturation knee
    // 'a' parameter controls the B-H curve knee location
    hScale_ = config_.material.a * 5.0f;

    // Normalize output: unity gain in linear region
    // At small H: susceptibility χ₀ ≈ Ms·c/a
    float chi0 = config_.material.Ms * config_.material.c / config_.material.a;
    float linearGainBperH = kMu0f * (1.0f + chi0);
    bNorm_ = 1.0f / (linearGainBperH * hScale_ + kEpsilonF);
  }
};

} // namespace transfo
