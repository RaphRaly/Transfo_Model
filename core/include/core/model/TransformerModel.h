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
#include "../wdf/WDFResonanceFilter.h"
#include "ToleranceModel.h"
#include "TransformerConfig.h"
#include <algorithm>
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

  // Linear mode: bypass J-A hysteresis entirely (HP → unity gain → LC).
  // Used for test baselines where a truly linear transfer function is needed.
  void setLinearMode(bool on) { linearMode_ = on; }
  bool getLinearMode() const { return linearMode_; }

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
    directDynLosses_.reset();
    hpState_ = 0.0f;
    hpPrev_ = 0.0f;
    lpState_ = 0.0f;
    lmSmoothed_ = config_.windings.Lp_primary; // Reset to static Lp
    lcFilter_.reset();
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
  DynamicLosses directDynLosses_;  // Bertotti dynamic losses for direct path
  float hScale_ = 100.0f;  // Input → H field scaling
  float bNorm_ = 1.0f;     // B → output normalization (unity gain in linear region)

  // ─── Circuit impedance filters ─────────────────────────────────────────
  // HP models source impedance / magnetizing inductance interaction:
  //   fc_hp = R_source / (2π × Lm) — bass rolloff, dynamically driven by Lm(t)
  float hpAlpha_ = 1.0f;   // HP coefficient (1.0 = near-bypass)
  float hpState_ = 0.0f;
  float hpPrev_ = 0.0f;

  // ─── LC resonance post-stage (replaces simple LP) — P1-2 ────────────
  // WDF-based second-order LC parasitic resonance filter.
  // Models Lleak + Ctotal interaction with optional Zobel damping.
  // Bypasses when Lleak ≈ 0 or Ctotal ≈ 0 (f_res >> audio band).
  WDFResonanceFilter lcFilter_;
  bool lcEnabled_ = false;

  // Legacy LP filter (kept for fallback when LC is disabled)
  float lpAlpha_ = 0.0f;   // LP coefficient (0.0 = bypass)
  float lpState_ = 0.0f;

  // ─── Dynamic Lm (magnetizing inductance) — P1-1 ──────────────────────
  // Lm(t) = K_geo * mu_inc(t), where mu_inc = mu0 * (1 + dM/dH)
  // dM/dH is the incremental susceptibility from the J-A model.
  // Lm dynamically drives the HP filter cutoff: fc = Rsource / (2π × Lm)
  float Rsource_ = 150.0f;       // Cached source impedance [Ohm]
  float Ts_hp_ = 1.0f / 44100.0f; // Cached 1/procRate for HP coefficient
  float lmSmoothed_ = 1.0f;      // One-pole smoothed Lm [H]
  float lmSmoothCoeff_ = 0.999f; // One-pole coefficient: α = exp(-2π×fc_smooth/fs)
  bool  dynLmEnabled_ = false;   // True when K_geo > 0 and Rsource > 0
  bool  linearMode_ = false;  // Bypass J-A: HP → linear gain → LC (test baselines)

  // Safety clamps for mu_inc (H/m)
  // mu_inc_min ~ mu0 (air): dM/dH ≈ 0 in deep saturation
  // mu_inc_max ~ mu0 * 200000: peak permeability of mu-metal
  static constexpr float kMuIncMin = 1.2566e-6f;          // mu0
  static constexpr float kMuIncMax = 1.2566e-6f * 200000; // mu0 * 200k
  static constexpr float kLmSmoothTimeMs = 2.0f;          // Smoothing time constant [ms]

  // ── Compute hpAlpha from dynamic Lm (called per-sample) ──────────────
  float computeHpAlphaFromLm(float Lm) const {
    // RC = Lm / Rsource → alpha = RC / (RC + Ts)
    const float RC = Lm / Rsource_;
    return RC / (RC + Ts_hp_);
  }

  // ─── Realtime processing — direct J-A bypass (no OS) ─────────────────────
  void processBlockRealtime(const float *input, float *output, int numSamples) {
    for (int k = 0; k < numSamples; ++k) {
      const float gain_in = inputGain_.getNextValue();
      const float gain_out = outputGain_.getNextValue();
      const float mixVal = mix_.getNextValue();

      const float dry = input[k];
      float x = input[k] * gain_in;

      // Circuit HP: source impedance / Lp bass rolloff
      const float hpOut = hpAlpha_ * (hpState_ + x - hpPrev_);
      hpPrev_ = x;
      hpState_ = hpOut;
      x = hpOut;

      float wet;
      if (linearMode_) {
        // Linear bypass: skip J-A, Bertotti, dynLm — unity gain after HP
        wet = x;
      } else {
      // Direct J-A: scale to H field, apply dynamic losses, solve hysteresis
      const float H_applied = x * hScale_;
      double H_eff = static_cast<double>(H_applied);

      // Bertotti dynamic extension: damped χ-scaling for self-consistent dBdt
      if (directDynLosses_.isEnabled()) {
        const double M_c = directHyst_.getMagnetization();
        const double chi = std::max(0.0, directHyst_.getInstantaneousSusceptibility());
        const double B_pred = kMu0 * (H_eff + M_c);
        const double dBdt_raw = directDynLosses_.computeDBdt(B_pred);
        const double G = directDynLosses_.getK1() * directDynLosses_.getSampleRate() * kMu0 * chi;
        // dBdt_raw already contains (1+χ) via B = µ0(H+M); only apply damping
        const double dBdt = dBdt_raw / (1.0 + G);
        H_eff -= directDynLosses_.computeHfromDBdt(dBdt);
      }

      const double M = directHyst_.solveImplicitStep(H_eff);
      directHyst_.commitState();

      // ── Dynamic Lm: update HP filter cutoff from J-A susceptibility ───
      if (dynLmEnabled_) {
        const double dMdH = directHyst_.getInstantaneousSusceptibility();
        // mu_inc = mu0 * (1 + dM/dH), clamped to physical range
        const float mu_inc = std::clamp(
            kMu0f * (1.0f + static_cast<float>(dMdH)),
            kMuIncMin, kMuIncMax);
        // Lm = K_geo * mu_inc
        const float Lm_raw = config_.geometry.computeLm(mu_inc);
        // One-pole smooth to avoid discontinuities / clicks
        lmSmoothed_ = lmSmoothCoeff_ * lmSmoothed_
                     + (1.0f - lmSmoothCoeff_) * Lm_raw;
        // Update HP coefficient from smoothed Lm
        hpAlpha_ = computeHpAlphaFromLm(lmSmoothed_);
      }

      // B uses total applied H (circuit field, not effective)
      const float B = kMu0f * (H_applied + static_cast<float>(M));
      directDynLosses_.commitState(static_cast<double>(B));

      wet = B * bNorm_;

      // BH scope data
      if (++bhDownsampleCounter_ >= 32) {
        bhDownsampleCounter_ = 0;
        bhQueue_.push(BHSample{H_applied, B});
      }
      } // end !linearMode_

      // HF shaping: LC resonance post-stage or legacy LP
      if (lcEnabled_) {
        wet = lcFilter_.processSample(wet);
      } else {
        lpState_ = (1.0f - lpAlpha_) * wet + lpAlpha_ * lpState_;
        wet = lpState_;
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
      float x = osBuffer[k] * inputGain_.getCurrentValue();

      // Circuit HP: source impedance / Lp bass rolloff
      const float hpOut = hpAlpha_ * (hpState_ + x - hpPrev_);
      hpPrev_ = x;
      hpState_ = hpOut;
      x = hpOut;

      float wet;
      if (linearMode_) {
        wet = x;
      } else {
      // Direct J-A: scale to H field, apply dynamic losses, solve hysteresis
      const float H_applied = x * hScale_;
      double H_eff = static_cast<double>(H_applied);

      // Bertotti dynamic extension: damped χ-scaling for self-consistent dBdt
      if (directDynLosses_.isEnabled()) {
        const double M_c = directHyst_.getMagnetization();
        const double chi = std::max(0.0, directHyst_.getInstantaneousSusceptibility());
        const double B_pred = kMu0 * (H_eff + M_c);
        const double dBdt_raw = directDynLosses_.computeDBdt(B_pred);
        const double G = directDynLosses_.getK1() * directDynLosses_.getSampleRate() * kMu0 * chi;
        // dBdt_raw already contains (1+χ) via B = µ0(H+M); only apply damping
        const double dBdt = dBdt_raw / (1.0 + G);
        H_eff -= directDynLosses_.computeHfromDBdt(dBdt);
      }

      const double M = directHyst_.solveImplicitStep(H_eff);
      directHyst_.commitState();

      // ── Dynamic Lm: update HP filter cutoff from J-A susceptibility ───
      if (dynLmEnabled_) {
        const double dMdH = directHyst_.getInstantaneousSusceptibility();
        const float mu_inc = std::clamp(
            kMu0f * (1.0f + static_cast<float>(dMdH)),
            kMuIncMin, kMuIncMax);
        const float Lm_raw = config_.geometry.computeLm(mu_inc);
        lmSmoothed_ = lmSmoothCoeff_ * lmSmoothed_
                     + (1.0f - lmSmoothCoeff_) * Lm_raw;
        hpAlpha_ = computeHpAlphaFromLm(lmSmoothed_);
      }

      // B uses total applied H (circuit field, not effective)
      const float B = kMu0f * (H_applied + static_cast<float>(M));
      directDynLosses_.commitState(static_cast<double>(B));

      wet = B * bNorm_;

      // BH scope data (less frequent for oversampled)
      if (++bhDownsampleCounter_ >= 128) {
        bhDownsampleCounter_ = 0;
        bhQueue_.push(BHSample{H_applied, B});
      }
      } // end !linearMode_

      // HF shaping: LC resonance post-stage or legacy LP
      if (lcEnabled_) {
        wet = lcFilter_.processSample(wet);
      } else {
        lpState_ = (1.0f - lpAlpha_) * wet + lpAlpha_ * lpState_;
        wet = lpState_;
      }
      osBuffer[k] = wet;
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

    // Bertotti dynamic losses for the direct path
    directDynLosses_.setCoefficients(config_.material.K1, config_.material.K2);
    directDynLosses_.setSampleRate(static_cast<double>(procRate));
    directDynLosses_.reset();

    // Scaling: map ±1.0 digital audio to H field around saturation knee
    // 'a' parameter controls the B-H curve knee location
    hScale_ = config_.material.a * 5.0f;

    // Normalize output: unity gain in linear region
    // Langevin L(x) ≈ x/3 for small x, so χ₀_raw = Ms·c/(3·a)
    // With alpha feedback: χ_eff = χ₀ / (1 - α·χ₀)
    float chi0 = config_.material.Ms * config_.material.c / (3.0f * config_.material.a);
    float denomFeedback = 1.0f - config_.material.alpha * chi0;
    if (denomFeedback < 0.1f) denomFeedback = 0.1f;  // Clamp near instability
    float chiEff = chi0 / denomFeedback;
    float linearGainBperH = kMu0f * (1.0f + chiEff);
    bNorm_ = 1.0f / (linearGainBperH * hScale_ + kEpsilonF);

    // ─── Circuit impedance filters ────────────────────────────────────────
    float Ts = 1.0f / procRate;
    Ts_hp_ = Ts;  // Cache for per-sample HP coefficient update

    // ── Dynamic Lm setup ──────────────────────────────────────────────────
    // Cache source impedance for per-sample HP update.
    // For tube output stages, plateImpedance carries the tube plate Z;
    // sourceImpedance carries only winding/circuit R for the LC filter.
    // The HP filter needs the total: sourceImpedance + plateImpedance.
    Rsource_ = config_.windings.sourceImpedance
             + config_.windings.plateImpedance;
    float K_geo = config_.geometry.K_geo;

    // Enable dynamic Lm if geometry and source impedance are valid
    dynLmEnabled_ = (Rsource_ > 0.0f && K_geo > 0.0f);

    // Smoothing coefficient: one-pole LPF with time constant kLmSmoothTimeMs
    // alpha = exp(-1 / (fs * tau)), tau = kLmSmoothTimeMs / 1000
    const float tau_smooth = kLmSmoothTimeMs * 0.001f;
    lmSmoothCoeff_ = std::exp(-Ts / tau_smooth);

    // Highpass: source impedance / magnetizing inductance
    // Initial Lm from the static small-signal susceptibility (linear region)
    // chi_eff was already computed above; use it for initial Lm
    if (dynLmEnabled_) {
      // Initial mu_inc from linear-region chi_eff
      float mu_inc_init = kMu0f * (1.0f + chiEff);
      mu_inc_init = std::clamp(mu_inc_init, kMuIncMin, kMuIncMax);
      float Lm_init = config_.geometry.computeLm(mu_inc_init);
      lmSmoothed_ = Lm_init;
      hpAlpha_ = computeHpAlphaFromLm(Lm_init);
    } else {
      // Fallback to static Lp_primary (legacy behavior)
      float Lp = config_.windings.Lp_primary;
      if (Rsource_ > 0.0f && Lp > 0.0f) {
        float RC_hp = Lp / Rsource_;
        hpAlpha_ = RC_hp / (RC_hp + Ts);
      } else {
        hpAlpha_ = 1.0f; // Bypass
      }
      lmSmoothed_ = config_.windings.Lp_primary;
    }

    // ── LC resonance post-stage (P1-2, P2-1) ─────────────────────────
    // WDF-based second-order LC resonance filter replaces simple LP.
    // P2-1: Uses corrected Ctotal accounting for Cp_s bridging when
    // no Faraday shield is present (Duerdoth / Miller effect).
    // Enabled when LC params produce a meaningful resonance (Lleak > 0,
    // Ctotal > 0). Otherwise falls back to simple LP.
    {
      const auto& lc = config_.lcParams;
      const float turnsRatio = config_.windings.turnsRatio();
      const bool hasShield = config_.windings.hasFaradayShield;
      const float Ct = lc.computeCtotalCorrected(turnsRatio, hasShield);
      const float Rs_lc = config_.windings.sourceImpedance
                        + config_.windings.Rdc_primary;
      const float Rload_lc = config_.loadImpedance;

      if (lc.Lleak > 1e-9f && Ct > 1e-15f) {
        lcFilter_.prepare(procRate, lc, Rs_lc, Rload_lc,
                          turnsRatio, hasShield);
        lcEnabled_ = true;
        lpAlpha_ = 0.0f; // Disable legacy LP
      } else {
        lcEnabled_ = false;
        // Fallback: simple LP from load / leakage
        float Rload = config_.loadImpedance;
        float Lleak = config_.windings.L_leakage;
        if (Rload > 0.0f && Lleak > 0.0f) {
          float RC_lp = Lleak / Rload;
          float fc_lp = 1.0f / (kTwoPif * RC_lp);
          if (fc_lp < procRate * 0.45f) {
            lpAlpha_ = std::exp(-kTwoPif * fc_lp * Ts);
          } else {
            lpAlpha_ = 0.0f;
          }
        } else {
          lpAlpha_ = 0.0f;
        }
      }
    }

    // Reset filter states
    hpState_ = 0.0f;
    hpPrev_ = 0.0f;
    lpState_ = 0.0f;
  }
};

} // namespace transfo
