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
#include "../../core/include/core/magnetics/JAParameterSet.h"
#include "CMA_ES.h"
#include "CPWLFitter.h"
#include "ConstraintSet.h"
#include "LevenbergMarquardt.h"
#include "MeasurementData.h"
#include "ObjectiveFunction.h"


#include <memory>

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

  // Phase 3: Export CPWL leaf for realtime mode
  void exportToRealtime(const JAParameterSet &params, float Gamma, float Lambda,
                        CPWLLeaf &outLeaf, int numSegments = 16) {
    CPWLFitter::generateCPWL(params, Gamma, Lambda, numSegments, outLeaf);
  }
};

} // namespace transfo
