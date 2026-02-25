#pragma once

// =============================================================================
// ActiveLearning [v3]
//
// An advanced identification module that maintains an ensemble of CMA-ES
// optimizers to estimate parameter uncertainty.
//
// Uses the ensemble variance to suggest the "Most Informative Measurement"
// (e.g. telling the user to measure a minor loop at specific amplitudes)
// to maximally reduce uncertainty in the identification process.
// =============================================================================

#include "../../core/include/core/magnetics/JAParameterSet.h"
#include "CMA_ES.h"


#include <iostream>
#include <memory>
#include <vector>


namespace transfo {

class ActiveLearning {
public:
  ActiveLearning(int ensembleSize = 5) : ensembleSize_(ensembleSize) {}

  void trainEnsemble(ObjectiveFunction &objective, const JABounds &bounds,
                     const JAParameterSet &initialGuess) {
    ensembleSolutions_.clear();
    for (int i = 0; i < ensembleSize_; ++i) {
      CMA_ES optimizer;
      optimizer.setRandomSeed(1234 + i); // different seed for diversity

      // In a real application, we might bootstrap the measurement data
      auto result = optimizer.optimize(objective, bounds, initialGuess);
      if (result.converged || result.iterations > 0)
        ensembleSolutions_.push_back(result.bestParams);
    }
  }

  // Suggests a measurement excitation amplitude that maximizes model
  // disagreement
  float suggestNextMeasurementAmplitude(float Hmax_current) {
    // Placeholder for variance-based active learning selection
    // Typical rule of thumb: If we only have major loop, suggest a minor loop
    // at ~40% of saturation
    return Hmax_current * 0.40f;
  }

private:
  int ensembleSize_;
  std::vector<JAParameterSet> ensembleSolutions_;
};

} // namespace transfo
