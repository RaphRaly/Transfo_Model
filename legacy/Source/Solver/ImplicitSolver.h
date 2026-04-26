#pragma once

// =============================================================================
// ImplicitSolver — Thin wrapper around the Newton-Raphson solving logic.
//
// In Phase 1, the actual NR solver lives inside HysteresisProcessor
// (following Chowdhury's approach where the solver is tightly coupled
// to the J-A equations for efficiency).
//
// This file exists as the future home for the Phase 2 multi-dimensional
// coupled system solver (CoupledSystemSolver), which will solve the full
// transformer circuit equations simultaneously.
//
// Phase 2 will add: CoupledSystemSolver.h/.cpp in this directory.
// =============================================================================

namespace ImplicitSolver
{

// Newton-Raphson configuration shared across components
struct Config
{
    int    maxIterations = 8;      // NR8 mode
    double tolerance     = 1e-12;  // Convergence threshold
};

} // namespace ImplicitSolver
