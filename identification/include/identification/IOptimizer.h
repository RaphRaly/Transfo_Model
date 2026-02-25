#pragma once

// =============================================================================
// IOptimizer -- Abstract interface for parameter identification optimizers.
//
// Defines the contract that CMA-ES, Levenberg-Marquardt, and any future
// optimizers must satisfy. Uses a Result struct to return the solution
// along with convergence diagnostics.
//
// Design rationale:
//   - Pure virtual interface allows pipeline to swap optimizers.
//   - Result struct carries everything needed for validation / reporting.
//   - Cancellation via cancel() enables UI responsiveness.
//   - Callback support for progress monitoring (GUI / CLI).
//
// Usage:
//   CMA_ES optimizer;
//   optimizer.configure(...);
//   auto result = optimizer.optimize(objective, bounds, initialGuess);
//   if (result.converged)
//       pipeline.useParameters(result.bestParams);
//
// Reference: Hansen 2006 (CMA-ES), More 1978 (Levenberg-Marquardt)
// =============================================================================

#include "../../core/include/core/magnetics/JAParameterSet.h"

#include <vector>
#include <string>
#include <functional>
#include <chrono>

namespace transfo {

// Forward declaration
class ObjectiveFunction;

// ---------------------------------------------------------------------------
// OptimizationResult -- returned by all optimizers.
// ---------------------------------------------------------------------------
struct OptimizationResult
{
    // ---- Solution ----
    JAParameterSet     bestParams;              // Best parameter set found
    float              bestCost      = 1e30f;   // Best objective value achieved

    // ---- Convergence info ----
    int                iterations    = 0;        // Total iterations performed
    int                functionEvals = 0;        // Total objective evaluations
    bool               converged     = false;    // True if convergence criterion met
    std::string        stopReason    = "";       // Human-readable reason for stopping

    // ---- Cost history (for diagnostics / plotting) ----
    std::vector<float> costHistory;              // Best cost at each iteration

    // ---- Timing ----
    double             elapsedSeconds = 0.0;     // Wall-clock time

    // ---- Uncertainty (CMA-ES specific, zero for others) ----
    std::vector<float> paramStdDevs;             // Per-parameter standard deviation
};

// ---------------------------------------------------------------------------
// ProgressCallback -- optional callback for monitoring optimization.
//
// Parameters: (iteration, bestCost, currentBestParams)
// Return true to continue, false to request early stop.
// ---------------------------------------------------------------------------
using ProgressCallback = std::function<bool(int iteration,
                                            float bestCost,
                                            const JAParameterSet& currentBest)>;

// ---------------------------------------------------------------------------
// IOptimizer -- abstract base class for all optimizers.
// ---------------------------------------------------------------------------
class IOptimizer
{
public:
    virtual ~IOptimizer() = default;

    // -----------------------------------------------------------------------
    // optimize -- run the optimization.
    //
    // Parameters:
    //   objective    : the objective function to minimize
    //   bounds       : parameter bounds (box constraints)
    //   initialGuess : starting point (may be ignored by global optimizers)
    //
    // Returns: OptimizationResult with solution and diagnostics.
    //
    // This is the primary entry point. Implementations must be reentrant
    // but not necessarily thread-safe (one optimize() call at a time).
    // -----------------------------------------------------------------------
    virtual OptimizationResult optimize(ObjectiveFunction& objective,
                                        const JABounds& bounds,
                                        const JAParameterSet& initialGuess) = 0;

    // -----------------------------------------------------------------------
    // cancel -- request early termination of an in-progress optimize() call.
    //
    // Thread-safe: can be called from a different thread than optimize().
    // After cancel(), optimize() should return as soon as practical with
    // the best result found so far. converged will be false.
    // -----------------------------------------------------------------------
    virtual void cancel() = 0;

    // -----------------------------------------------------------------------
    // setProgressCallback -- register a callback for progress updates.
    //
    // The callback is invoked once per iteration (or a subset of iterations
    // for high-frequency optimizers). If the callback returns false,
    // optimization stops (equivalent to cancel()).
    // -----------------------------------------------------------------------
    virtual void setProgressCallback(ProgressCallback callback)
    {
        progressCallback_ = std::move(callback);
    }

    // -----------------------------------------------------------------------
    // getName -- return a human-readable optimizer name (for reporting).
    // -----------------------------------------------------------------------
    virtual std::string getName() const = 0;

protected:
    ProgressCallback progressCallback_;

    // Utility: notify progress callback. Returns false if user requested stop.
    bool notifyProgress(int iteration, float bestCost, const JAParameterSet& best)
    {
        if (progressCallback_)
            return progressCallback_(iteration, bestCost, best);
        return true;  // No callback = continue
    }
};

} // namespace transfo
