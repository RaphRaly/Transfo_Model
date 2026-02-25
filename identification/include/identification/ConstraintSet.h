#pragma once

// =============================================================================
// ConstraintSet
//
// Implements physical constraints and bound penalties for the identification
// process. Ensures that candidate solutions proposed by global optimizers
// (like CMA-ES) remain physically valid.
//
// Critical constraint: k > alpha * Ms (Thermodynamic consistency)
// =============================================================================

#include "../../core/include/core/magnetics/JAParameterSet.h"
#include <algorithm>
#include <cmath>


namespace transfo {

class ConstraintSet {
public:
  // Computes a penalty value (to be added to the objective cost)
  // if parameters violate bounds or physical constraints.
  static float computePenalty(const JAParameterSet &params,
                              const JABounds &bounds) {
    float penalty = 0.0f;

    // 1. Box constraint penalties (squared distance from bound)
    if (params.Ms < bounds.Ms_min)
      penalty += std::pow((bounds.Ms_min - params.Ms) / bounds.Ms_min, 2.0f);
    if (params.Ms > bounds.Ms_max)
      penalty += std::pow((params.Ms - bounds.Ms_max) / bounds.Ms_max, 2.0f);

    if (params.a < bounds.a_min)
      penalty += std::pow((bounds.a_min - params.a) / bounds.a_min, 2.0f);
    if (params.a > bounds.a_max)
      penalty += std::pow((params.a - bounds.a_max) / bounds.a_max, 2.0f);

    if (params.k < bounds.k_min)
      penalty += std::pow((bounds.k_min - params.k) / bounds.k_min, 2.0f);
    if (params.k > bounds.k_max)
      penalty += std::pow((params.k - bounds.k_max) / bounds.k_max, 2.0f);

    if (params.alpha < bounds.alpha_min)
      penalty +=
          std::pow((bounds.alpha_min - params.alpha) / bounds.alpha_min, 2.0f);
    if (params.alpha > bounds.alpha_max)
      penalty +=
          std::pow((params.alpha - bounds.alpha_max) / bounds.alpha_max, 2.0f);

    if (params.c < bounds.c_min)
      penalty += std::pow(bounds.c_min - params.c, 2.0f);
    if (params.c > bounds.c_max)
      penalty += std::pow(params.c - bounds.c_max, 2.0f);

    // 2. Physical constraint penalty: k > alpha * Ms
    // Provide a 5% margin to avoid pushing the solver right to the edge
    const float margin = 1.05f;
    const float min_k = margin * params.alpha * params.Ms;
    if (params.k < min_k) {
      float diffRatio = (min_k - params.k) / min_k;
      penalty += diffRatio * diffRatio *
                 100.0f; // Strong penalty for physical violation
    }

    // Return a heavily scaled penalty so the optimizer quickly rejects this
    // region
    return penalty * 1e6f;
  }
};

} // namespace transfo
