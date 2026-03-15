#pragma once

// =============================================================================
// IdentificationPipeline
//
// Orchestrates the entire parameter identification workflow:
//
//   Phase 0: Initialization (load JSON, validate bounds)
//   Phase 1: Global Search (CMA-ES)
//   Phase 2: Local Refinement (Levenberg-Marquardt polish)
//   Phase 3: Validation & CPWL extraction
// =============================================================================

#include "../../core/include/core/magnetics/CPWLLeaf.h"
#include "../../core/include/core/magnetics/DynamicLosses.h"
#include "../../core/include/core/magnetics/JAParameterSet.h"
#include "CMA_ES.h"
#include "CPWLFitter.h"
#include "ConstraintSet.h"
#include "LevenbergMarquardt.h"
#include "MeasurementData.h"
#include "ObjectiveFunction.h"


#include <cmath>
#include <memory>
#include <vector>

namespace transfo {

class IdentificationPipeline {
public:
  IdentificationPipeline() = default;

  struct PipelineResult {
    JAParameterSet params;
    float finalError;
    bool success;
  };

  PipelineResult run(const MeasurementData &measData,
                     MaterialFamily family = MaterialFamily::MuMetal_80NiFe) {
    PipelineResult res;
    res.success = false;

    // Phase 0: Setup
    ObjectiveFunction obj;
    obj.setMeasurementData(measData);
    JABounds bounds = JAParameterSet::getDefaultBounds(family);

    // Use default as initial guess based on family
    JAParameterSet initialGuess;
    if (family == MaterialFamily::MuMetal_80NiFe)
      initialGuess = JAParameterSet::defaultMuMetal();
    else if (family == MaterialFamily::NiFe_50)
      initialGuess = JAParameterSet::defaultNiFe50();
    else
      initialGuess = JAParameterSet::defaultSiFe();

    // Phase 1: CMA-ES
    CMA_ES cma;
    auto cmaResult = cma.optimize(obj, bounds, initialGuess);

    if (!cmaResult.converged && cmaResult.iterations == 0) {
      return res; // Interrupted or failed
    }

    // Phase 2: Levenberg-Marquardt Polish
    LevenbergMarquardt lm;
    auto lmResult = lm.optimize(obj, bounds, cmaResult.bestParams);

    res.params = lmResult.bestParams;
    res.finalError = lmResult.bestCost;
    res.success = true;

    return res;
  }

  // ─── Phase 2: Dynamic identification (K1/K2 from multi-frequency data) ──
  //
  // Freezes the 5 static J-A params (from Phase 1) and optimizes K1/K2
  // to minimize the B-H loop area error across multiple frequencies.
  //
  // Requires multi-frequency measurement data (B-H loops at 2+ frequencies).
  // Falls back to simplified estimation if no multi-frequency data.
  //
  // Reference: Bertotti 1998; MIT 6.003 Sol 15.5 (least-squares inverse)
  PipelineResult runDynamic(const std::vector<MeasurementData> &multiFreqData,
                            const JAParameterSet &staticParams,
                            MaterialFamily family = MaterialFamily::MuMetal_80NiFe) {
    PipelineResult res;
    res.params = staticParams;
    res.success = false;

    if (multiFreqData.empty()) {
      res.success = false;
      return res;
    }

    // Objective: MultiFreqError at each measured frequency
    ObjectiveFunction obj;
    obj.setMeasurementData(multiFreqData[0]); // Primary data for shared state

    for (const auto &meas : multiFreqData) {
      ComponentConfig cfg;
      cfg.type = ObjectiveComponent::MultiFreqError;
      cfg.frequencyHz = static_cast<float>(meas.getMetadata().frequencyHz);
      cfg.weight = 1.0f;
      cfg.enabled = true;
      obj.addComponent(cfg);
    }

    // Add BH closure penalty
    obj.addComponent(ObjectiveComponent::BHClosurePenalty, 10.0f);

    // Bounds: freeze static params (min == max), vary K1/K2
    JABounds bounds = JAParameterSet::getDefaultBounds(family);

    // Freeze static params by setting bounds to the frozen values + tiny epsilon
    const float eps = 1e-6f;
    bounds.Ms_min = staticParams.Ms * (1.0f - eps);
    bounds.Ms_max = staticParams.Ms * (1.0f + eps);
    bounds.a_min = staticParams.a * (1.0f - eps);
    bounds.a_max = staticParams.a * (1.0f + eps);
    bounds.k_min = staticParams.k * (1.0f - eps);
    bounds.k_max = staticParams.k * (1.0f + eps);
    bounds.alpha_min = std::max(1e-10f, staticParams.alpha * (1.0f - eps));
    bounds.alpha_max = staticParams.alpha * (1.0f + eps);
    bounds.c_min = std::max(0.01f, staticParams.c - eps);
    bounds.c_max = std::min(0.99f, staticParams.c + eps);
    // K1/K2 bounds from material family (already set by getDefaultBounds)

    // CMA-ES on the full 7D (5 frozen + 2 free)
    CMA_ES cma;
    cma.setMaxGenerations(200);   // Fewer gens needed for 2 effective dims
    cma.setMaxFunctionEvals(10000);
    cma.setInitialSigma(0.3f);

    auto cmaResult = cma.optimize(obj, bounds, staticParams);

    res.params = cmaResult.bestParams;
    res.finalError = cmaResult.bestCost;
    res.success = cmaResult.converged ||
                  (cmaResult.iterations > 0 && cmaResult.bestCost < 1e8f);

    return res;
  }

  // ─── Simplified K_eddy estimation (no measurement data needed) ──────────
  //
  // Estimates K1 from lamination thickness d [m] and resistivity rho [Ohm·m].
  // K2 is estimated empirically as K2 ≈ 10 * sqrt(K1).
  //
  //   K1 = d² / (12 * rho)
  //   K2 ≈ 10 * sqrt(K1)  (empirical, Bertotti 1998)
  //
  // Typical values:
  //   Mu-metal:  d=0.1mm, rho=58e-8 → K1=1.44e-3, K2≈0.38  (use 0.02 for audio)
  //   NiFe 50%:  d=0.15mm, rho=46e-8 → K1=4.08e-3, K2≈0.64 (use 0.06 for audio)
  //   GO SiFe:   d=0.35mm, rho=48e-8 → K1=2.13e-2, K2≈1.46 (use 0.15 for audio)
  //
  // Note: the empirical K2 formula overestimates for audio transformers.
  // The values in JAParameterSet presets are tuned for audio frequencies.
  static void estimateFromMaterial(float d_meters, float rho_ohm_m,
                                   float &K1_out, float &K2_out) {
    K1_out = DynamicLosses::computeKeddy(d_meters, rho_ohm_m);
    // Empirical K2 for audio transformers (scaled down from Bertotti's formula)
    K2_out = 0.5f * std::sqrt(K1_out);
  }

  // Phase 3: Export CPWL leaf for realtime mode
  void exportToRealtime(const JAParameterSet &params, float Gamma, float Lambda,
                        CPWLLeaf &outLeaf, int numSegments = 16) {
    CPWLFitter::generateCPWL(params, Gamma, Lambda, numSegments, outLeaf);
  }
};

} // namespace transfo
