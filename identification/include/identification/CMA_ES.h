#pragma once

// =============================================================================
// CMA_ES [v3 enriched] -- Covariance Matrix Adaptation Evolution Strategy.
//
// A derivative-free, global optimization algorithm well-suited for the
// multi-modal, noisy landscape of Jiles-Atherton parameter identification.
//
// Key features:
//   - Population-based: escapes local minima
//   - Covariance matrix learns the local Hessian structure
//   - Step-size adaptation (cumulative path length control)
//   - Log-reparametrization [v3]: optimizes in log(Ms, a, k) + sigmoid(c) space
//     to guarantee positivity and natural scaling
//   - Simplified but functional implementation (no external dependency)
//
// Algorithm outline (Hansen 2006):
//   1. Sample lambda offspring: x_i = mean + sigma * N(0, C)
//   2. Evaluate fitness of each offspring
//   3. Select mu best (weighted recombination)
//   4. Update mean, evolution paths, covariance matrix, step size
//   5. Repeat until convergence or budget exhausted
//
// The covariance matrix is stored as a flat vector<float> representing the
// upper triangle, with eigendecomposition performed every ~N iterations
// for efficiency (rank-mu update only needs the triangle).
//
// Reference: Hansen 2006 "The CMA Evolution Strategy: A Tutorial";
//            Hansen & Ostermeier 2001; Szewczyk 2020 (J-A context)
// =============================================================================

#include "IOptimizer.h"
#include "ObjectiveFunction.h"
#include "../../core/include/core/magnetics/JAParameterSet.h"
#include "../../core/include/core/util/Constants.h"

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <atomic>
#include <chrono>
#include <string>
#include <cassert>

namespace transfo {

// ---------------------------------------------------------------------------
// CMA_ES -- Covariance Matrix Adaptation Evolution Strategy.
// ---------------------------------------------------------------------------
class CMA_ES : public IOptimizer
{
public:
    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    // Set population size (lambda). Default = 4 + floor(3 * ln(N)).
    // Larger populations improve exploration but slow convergence.
    void setPopulationSize(int popSize) { pop_size_ = popSize; }

    // Set initial step size (sigma0). Default = 0.5.
    // Larger sigma -> more exploration. Typical range: [0.1, 2.0].
    void setInitialSigma(float sigma) { sigma0_ = sigma; }

    // Set maximum number of generations. Default = 500.
    void setMaxGenerations(int maxGen) { maxGenerations_ = maxGen; }

    // Set convergence tolerance on fitness improvement.
    // Stops when best fitness changes by less than tol for tolStagnationGens.
    void setFitnessTolerance(float tol) { fitnessTol_ = tol; }

    // Set maximum function evaluations. Default = 50000.
    void setMaxFunctionEvals(int maxEvals) { maxFunEvals_ = maxEvals; }

    // [v3] Enable log-reparametrization.
    //
    // When enabled, optimization operates in transformed space:
    //   log(Ms), log(a), log(k), log(alpha), logit(c)
    //
    // This guarantees positivity of Ms, a, k, alpha and c in [0,1]
    // without explicit constraint handling, improving CMA-ES convergence.
    void setLogReparametrization(bool enable) { logReparametrize_ = enable; }

    // Set random seed for reproducibility. 0 = use random device.
    void setRandomSeed(unsigned int seed) { seed_ = seed; }

    // -----------------------------------------------------------------------
    // IOptimizer interface
    // -----------------------------------------------------------------------

    OptimizationResult optimize(ObjectiveFunction& objective,
                                const JABounds& bounds,
                                const JAParameterSet& initialGuess) override
    {
        cancelRequested_.store(false);
        auto tStart = std::chrono::steady_clock::now();

        const int N = JAParameterSet::kNumOptParams;  // Dimension = 5

        // --- Initialize random generator ---
        std::mt19937 rng(seed_ != 0 ? seed_ : std::random_device{}());
        std::normal_distribution<float> normal(0.0f, 1.0f);

        // --- Population parameters ---
        int lambda = (pop_size_ > 0) ? pop_size_ : (4 + static_cast<int>(3.0f * std::log(static_cast<float>(N))));
        int mu     = lambda / 2;    // Number of parents for recombination

        // --- Recombination weights (log-linear) ---
        std::vector<float> weights(static_cast<size_t>(mu));
        float sumW = 0.0f;
        for (int i = 0; i < mu; ++i)
        {
            weights[static_cast<size_t>(i)] = std::log(static_cast<float>(mu) + 0.5f)
                                             - std::log(static_cast<float>(i + 1));
            sumW += weights[static_cast<size_t>(i)];
        }
        // Normalize weights
        for (int i = 0; i < mu; ++i)
            weights[static_cast<size_t>(i)] /= sumW;

        // Variance-effective selection mass
        float muEff = 0.0f;
        for (int i = 0; i < mu; ++i)
            muEff += weights[static_cast<size_t>(i)] * weights[static_cast<size_t>(i)];
        muEff = 1.0f / muEff;

        // --- Strategy parameters (Hansen 2006 defaults) ---
        float cc    = (4.0f + muEff / static_cast<float>(N))
                    / (static_cast<float>(N) + 4.0f + 2.0f * muEff / static_cast<float>(N));
        float cs    = (muEff + 2.0f) / (static_cast<float>(N) + muEff + 5.0f);
        float c1    = 2.0f / ((static_cast<float>(N) + 1.3f) * (static_cast<float>(N) + 1.3f) + muEff);
        float cmu   = std::min(1.0f - c1,
                      2.0f * (muEff - 2.0f + 1.0f / muEff)
                      / ((static_cast<float>(N) + 2.0f) * (static_cast<float>(N) + 2.0f) + muEff));
        float damps = 1.0f + 2.0f * std::max(0.0f, std::sqrt((muEff - 1.0f) / (static_cast<float>(N) + 1.0f)) - 1.0f) + cs;

        float chiN  = std::sqrt(static_cast<float>(N))
                    * (1.0f - 1.0f / (4.0f * static_cast<float>(N))
                       + 1.0f / (21.0f * static_cast<float>(N) * static_cast<float>(N)));

        // --- Initialize state ---
        // Mean (in optimization space)
        std::array<float, 5> mean;
        if (logReparametrize_)
            mean = initialGuess.toLogSpace();
        else
            mean = {initialGuess.Ms, initialGuess.a, initialGuess.k, initialGuess.alpha, initialGuess.c};

        float sigma = sigma0_;

        // Covariance matrix C = I initially (stored as flat NxN)
        std::vector<float> C(static_cast<size_t>(N * N), 0.0f);
        for (int i = 0; i < N; ++i)
            C[static_cast<size_t>(i * N + i)] = 1.0f;

        // Square root of C (for sampling). Initially = I.
        std::vector<float> sqrtC(static_cast<size_t>(N * N), 0.0f);
        for (int i = 0; i < N; ++i)
            sqrtC[static_cast<size_t>(i * N + i)] = 1.0f;

        // Eigenvalues of C
        std::vector<float> eigenvalues(static_cast<size_t>(N), 1.0f);

        // Evolution paths
        std::vector<float> pc(static_cast<size_t>(N), 0.0f);  // Covariance path
        std::vector<float> ps(static_cast<size_t>(N), 0.0f);  // Step-size path

        // --- Population storage ---
        std::vector<std::array<float, 5>> population(static_cast<size_t>(lambda));
        std::vector<float> fitness(static_cast<size_t>(lambda));
        std::vector<int> ranking(static_cast<size_t>(lambda));

        // --- Result tracking ---
        OptimizationResult result;
        result.bestCost   = 1e30f;
        result.bestParams = initialGuess;

        int eigendecompInterval = std::max(1, N / 2);  // Recompute eigendecomposition periodically
        int totalEvals = 0;

        // ===================================================================
        // MAIN LOOP
        // ===================================================================
        for (int gen = 0; gen < maxGenerations_; ++gen)
        {
            if (cancelRequested_.load())
            {
                result.stopReason = "Cancelled by user";
                break;
            }

            // --- Eigendecomposition of C (every eigendecompInterval gens) ---
            if (gen % eigendecompInterval == 0)
                computeEigendecomposition(C, sqrtC, eigenvalues, N);

            // --- Sample offspring ---
            for (int i = 0; i < lambda; ++i)
            {
                // z ~ N(0, I)
                std::array<float, 5> z;
                for (int d = 0; d < N; ++d)
                    z[static_cast<size_t>(d)] = normal(rng);

                // x = mean + sigma * sqrtC * z
                for (int d = 0; d < N; ++d)
                {
                    float val = mean[static_cast<size_t>(d)];
                    for (int j = 0; j < N; ++j)
                        val += sigma * sqrtC[static_cast<size_t>(d * N + j)] * z[static_cast<size_t>(j)];
                    population[static_cast<size_t>(i)][static_cast<size_t>(d)] = val;
                }

                // --- Enforce bounds (in optimization space) ---
                if (logReparametrize_)
                {
                    // In log space, bounds become log(bounds)
                    population[static_cast<size_t>(i)][0] = std::clamp(population[static_cast<size_t>(i)][0],
                        std::log(bounds.Ms_min), std::log(bounds.Ms_max));
                    population[static_cast<size_t>(i)][1] = std::clamp(population[static_cast<size_t>(i)][1],
                        std::log(bounds.a_min), std::log(bounds.a_max));
                    population[static_cast<size_t>(i)][2] = std::clamp(population[static_cast<size_t>(i)][2],
                        std::log(bounds.k_min), std::log(bounds.k_max));
                    population[static_cast<size_t>(i)][3] = std::clamp(population[static_cast<size_t>(i)][3],
                        std::log(bounds.alpha_min + 1e-10f), std::log(bounds.alpha_max));
                    // c (logit space): no explicit bound clamp needed, sigmoid maps R -> (0,1)
                }
                else
                {
                    population[static_cast<size_t>(i)][0] = std::clamp(population[static_cast<size_t>(i)][0],
                        bounds.Ms_min, bounds.Ms_max);
                    population[static_cast<size_t>(i)][1] = std::clamp(population[static_cast<size_t>(i)][1],
                        bounds.a_min, bounds.a_max);
                    population[static_cast<size_t>(i)][2] = std::clamp(population[static_cast<size_t>(i)][2],
                        bounds.k_min, bounds.k_max);
                    population[static_cast<size_t>(i)][3] = std::clamp(population[static_cast<size_t>(i)][3],
                        bounds.alpha_min, bounds.alpha_max);
                    population[static_cast<size_t>(i)][4] = std::clamp(population[static_cast<size_t>(i)][4],
                        bounds.c_min, bounds.c_max);
                }

                // --- Evaluate fitness ---
                JAParameterSet candidate = decodeParams(population[static_cast<size_t>(i)]);
                fitness[static_cast<size_t>(i)] = objective.evaluate(candidate);
                ++totalEvals;

                // Track best
                if (fitness[static_cast<size_t>(i)] < result.bestCost)
                {
                    result.bestCost   = fitness[static_cast<size_t>(i)];
                    result.bestParams = candidate;
                }
            }

            // --- Sort by fitness (ascending = best first) ---
            std::iota(ranking.begin(), ranking.end(), 0);
            std::sort(ranking.begin(), ranking.end(),
                [&fitness](int a, int b) {
                    return fitness[static_cast<size_t>(a)] < fitness[static_cast<size_t>(b)];
                });

            // --- Record cost history ---
            result.costHistory.push_back(result.bestCost);

            // --- Progress callback ---
            if (!notifyProgress(gen, result.bestCost, result.bestParams))
            {
                result.stopReason = "Stopped by progress callback";
                break;
            }

            // --- Weighted mean of best mu individuals ---
            std::array<float, 5> oldMean = mean;
            for (int d = 0; d < N; ++d)
            {
                mean[static_cast<size_t>(d)] = 0.0f;
                for (int i = 0; i < mu; ++i)
                {
                    int idx = ranking[static_cast<size_t>(i)];
                    mean[static_cast<size_t>(d)] += weights[static_cast<size_t>(i)]
                                                  * population[static_cast<size_t>(idx)][static_cast<size_t>(d)];
                }
            }

            // --- Update evolution path for step-size control (ps) ---
            // Inverse square root of C applied to (mean - oldMean)
            std::vector<float> artmp(static_cast<size_t>(N));
            for (int d = 0; d < N; ++d)
                artmp[static_cast<size_t>(d)] = (mean[static_cast<size_t>(d)] - oldMean[static_cast<size_t>(d)]) / sigma;

            // ps = (1-cs)*ps + sqrt(cs*(2-cs)*mueff) * invsqrtC * artmp
            // For simplicity, use identity approximation for invsqrtC (exact when C ~ I)
            std::vector<float> invsqrtC_artmp(static_cast<size_t>(N), 0.0f);
            {
                // Compute C^{-1/2} * artmp using eigendecomposition
                // invsqrtC * v = U * diag(1/sqrt(eigenvalues)) * U^T * v
                // For simplicity, apply the stored sqrtC inverse approximation
                // We use the fact that for near-identity C, invsqrtC ~ I - 0.5*(C-I)
                // Better: store inverse sqrt explicitly after eigendecomposition
                for (int d = 0; d < N; ++d)
                {
                    float val = 0.0f;
                    for (int j = 0; j < N; ++j)
                    {
                        float invSqrtEig = (eigenvalues[static_cast<size_t>(j)] > 1e-20f)
                                         ? 1.0f / std::sqrt(eigenvalues[static_cast<size_t>(j)])
                                         : 1.0f;
                        // sqrtC stores eigenvectors column-wise
                        // invsqrtC_artmp[d] = sum_j (sqrtC[d][j] * invSqrtEig_j * dot(sqrtC^T[j], artmp))
                        float dot = 0.0f;
                        for (int k = 0; k < N; ++k)
                            dot += sqrtC[static_cast<size_t>(k * N + j)] * artmp[static_cast<size_t>(k)];
                        val += sqrtC[static_cast<size_t>(d * N + j)] * invSqrtEig * dot;
                    }
                    invsqrtC_artmp[static_cast<size_t>(d)] = val;
                }
            }

            float sqrtCsMuEff = std::sqrt(cs * (2.0f - cs) * muEff);
            for (int d = 0; d < N; ++d)
            {
                ps[static_cast<size_t>(d)] = (1.0f - cs) * ps[static_cast<size_t>(d)]
                                           + sqrtCsMuEff * invsqrtC_artmp[static_cast<size_t>(d)];
            }

            // --- Update evolution path for covariance (pc) ---
            float psNorm = 0.0f;
            for (int d = 0; d < N; ++d)
                psNorm += ps[static_cast<size_t>(d)] * ps[static_cast<size_t>(d)];
            psNorm = std::sqrt(psNorm);

            float hsig = (psNorm / std::sqrt(1.0f - std::pow(1.0f - cs, 2.0f * static_cast<float>(gen + 1)))
                         / chiN) < (1.4f + 2.0f / (static_cast<float>(N) + 1.0f)) ? 1.0f : 0.0f;

            float sqrtCcMuEff = std::sqrt(cc * (2.0f - cc) * muEff);
            for (int d = 0; d < N; ++d)
            {
                pc[static_cast<size_t>(d)] = (1.0f - cc) * pc[static_cast<size_t>(d)]
                                           + hsig * sqrtCcMuEff * artmp[static_cast<size_t>(d)];
            }

            // --- Covariance matrix adaptation ---
            // C = (1-c1-cmu)*C + c1*(pc*pc^T + (1-hsig)*cc*(2-cc)*C) + cmu*sum(w_i * y_i*y_i^T)
            float c1a = c1 * (1.0f - (1.0f - hsig * hsig) * cc * (2.0f - cc));

            for (int i = 0; i < N; ++i)
            {
                for (int j = 0; j <= i; ++j)
                {
                    size_t ij = static_cast<size_t>(i * N + j);

                    // Old covariance contribution
                    float newC = (1.0f - c1a - cmu) * C[ij];

                    // Rank-1 update: pc * pc^T
                    newC += c1 * pc[static_cast<size_t>(i)] * pc[static_cast<size_t>(j)];

                    // Rank-mu update: weighted sum of y_i * y_i^T
                    for (int k = 0; k < mu; ++k)
                    {
                        int idx = ranking[static_cast<size_t>(k)];
                        float yi = (population[static_cast<size_t>(idx)][static_cast<size_t>(i)] - oldMean[static_cast<size_t>(i)]) / sigma;
                        float yj = (population[static_cast<size_t>(idx)][static_cast<size_t>(j)] - oldMean[static_cast<size_t>(j)]) / sigma;
                        newC += cmu * weights[static_cast<size_t>(k)] * yi * yj;
                    }

                    C[ij] = newC;
                    C[static_cast<size_t>(j * N + i)] = newC;  // Symmetry
                }
            }

            // --- Step-size update ---
            sigma *= std::exp((cs / damps) * (psNorm / chiN - 1.0f));

            // Clamp sigma to avoid numerical issues
            sigma = std::clamp(sigma, 1e-10f, 1e6f);

            // --- Convergence checks ---
            // 1. Function evaluation budget
            if (totalEvals >= maxFunEvals_)
            {
                result.stopReason = "Max function evaluations reached";
                break;
            }

            // 2. Fitness stagnation
            if (result.costHistory.size() > 20)
            {
                size_t sz = result.costHistory.size();
                float recent = result.costHistory[sz - 1];
                float older  = result.costHistory[sz - 20];
                if (std::abs(older - recent) < fitnessTol_)
                {
                    result.converged  = true;
                    result.stopReason = "Fitness stagnation (converged)";
                    break;
                }
            }

            // 3. Step size too small
            if (sigma < 1e-12f)
            {
                result.converged  = true;
                result.stopReason = "Step size below threshold (converged)";
                break;
            }

            // 4. Condition number of C too large
            float maxEig = *std::max_element(eigenvalues.begin(), eigenvalues.end());
            float minEig = *std::min_element(eigenvalues.begin(), eigenvalues.end());
            if (minEig > 0.0f && maxEig / minEig > 1e14f)
            {
                result.stopReason = "Covariance matrix ill-conditioned";
                break;
            }
        }

        // --- Finalize result ---
        auto tEnd = std::chrono::steady_clock::now();
        result.elapsedSeconds = std::chrono::duration<double>(tEnd - tStart).count();
        result.iterations     = static_cast<int>(result.costHistory.size());
        result.functionEvals  = totalEvals;

        // Parameter standard deviations from covariance matrix
        result.paramStdDevs.resize(static_cast<size_t>(JAParameterSet::kNumOptParams));
        for (int d = 0; d < JAParameterSet::kNumOptParams; ++d)
            result.paramStdDevs[static_cast<size_t>(d)] = sigma * std::sqrt(C[static_cast<size_t>(d * JAParameterSet::kNumOptParams + d)]);

        if (result.stopReason.empty())
            result.stopReason = "Max generations reached";

        return result;
    }

    void cancel() override
    {
        cancelRequested_.store(true);
    }

    std::string getName() const override
    {
        return "CMA-ES" + std::string(logReparametrize_ ? " (log-reparametrized)" : "");
    }

private:
    // Configuration
    int   pop_size_        = 0;       // 0 = auto (4 + 3*ln(N))
    float sigma0_          = 0.5f;    // Initial step size
    int   maxGenerations_  = 500;
    float fitnessTol_      = 1e-8f;
    int   maxFunEvals_     = 50000;
    bool  logReparametrize_ = true;   // [v3] default ON
    unsigned int seed_     = 0;

    std::atomic<bool> cancelRequested_{false};

    // -----------------------------------------------------------------------
    // decodeParams -- convert optimization-space vector to JAParameterSet.
    // -----------------------------------------------------------------------
    JAParameterSet decodeParams(const std::array<float, 5>& x) const
    {
        if (logReparametrize_)
            return JAParameterSet::fromLogSpace(x);

        JAParameterSet p;
        p.Ms    = x[0];
        p.a     = x[1];
        p.k     = x[2];
        p.alpha = x[3];
        p.c     = x[4];
        return p;
    }

    // -----------------------------------------------------------------------
    // computeEigendecomposition -- simplified eigendecomposition of C.
    //
    // For the 5x5 symmetric covariance matrix, we use the Jacobi
    // eigenvalue algorithm (simple, robust, exact for small N).
    //
    // Outputs:
    //   sqrtC      : C^{1/2} = U * diag(sqrt(eigenvalues)) * U^T
    //   eigenvalues: eigenvalues of C
    // -----------------------------------------------------------------------
    static void computeEigendecomposition(const std::vector<float>& C,
                                           std::vector<float>& sqrtC,
                                           std::vector<float>& eigenvalues,
                                           int N)
    {
        // Copy C into working matrix A (will be destroyed)
        std::vector<float> A = C;

        // Initialize eigenvector matrix V = I
        std::vector<float> V(static_cast<size_t>(N * N), 0.0f);
        for (int i = 0; i < N; ++i)
            V[static_cast<size_t>(i * N + i)] = 1.0f;

        // Jacobi rotation method
        const int maxSweeps = 50;
        for (int sweep = 0; sweep < maxSweeps; ++sweep)
        {
            // Check for convergence: sum of off-diagonal^2
            float offDiagSum = 0.0f;
            for (int i = 0; i < N; ++i)
                for (int j = i + 1; j < N; ++j)
                    offDiagSum += A[static_cast<size_t>(i * N + j)] * A[static_cast<size_t>(i * N + j)];

            if (offDiagSum < 1e-20f)
                break;

            for (int p = 0; p < N - 1; ++p)
            {
                for (int q = p + 1; q < N; ++q)
                {
                    float Apq = A[static_cast<size_t>(p * N + q)];
                    if (std::abs(Apq) < 1e-15f)
                        continue;

                    float App = A[static_cast<size_t>(p * N + p)];
                    float Aqq = A[static_cast<size_t>(q * N + q)];

                    // Compute rotation angle
                    float tau = (Aqq - App) / (2.0f * Apq);
                    float t;
                    if (std::abs(tau) > 1e15f)
                        t = 1.0f / (2.0f * tau);
                    else
                        t = ((tau >= 0.0f) ? 1.0f : -1.0f)
                          / (std::abs(tau) + std::sqrt(1.0f + tau * tau));

                    float cosTheta = 1.0f / std::sqrt(1.0f + t * t);
                    float sinTheta = t * cosTheta;
                    float tanU = sinTheta / (1.0f + cosTheta);

                    // Update A
                    A[static_cast<size_t>(p * N + p)] -= t * Apq;
                    A[static_cast<size_t>(q * N + q)] += t * Apq;
                    A[static_cast<size_t>(p * N + q)] = 0.0f;
                    A[static_cast<size_t>(q * N + p)] = 0.0f;

                    // Rotate rows/columns p and q
                    for (int r = 0; r < N; ++r)
                    {
                        if (r == p || r == q) continue;

                        float Arp = A[static_cast<size_t>(r * N + p)];
                        float Arq = A[static_cast<size_t>(r * N + q)];

                        A[static_cast<size_t>(r * N + p)] = Arp - sinTheta * (Arq + tanU * Arp);
                        A[static_cast<size_t>(r * N + q)] = Arq + sinTheta * (Arp - tanU * Arq);
                        A[static_cast<size_t>(p * N + r)] = A[static_cast<size_t>(r * N + p)];
                        A[static_cast<size_t>(q * N + r)] = A[static_cast<size_t>(r * N + q)];
                    }

                    // Update eigenvector matrix V
                    for (int r = 0; r < N; ++r)
                    {
                        float Vrp = V[static_cast<size_t>(r * N + p)];
                        float Vrq = V[static_cast<size_t>(r * N + q)];
                        V[static_cast<size_t>(r * N + p)] = Vrp - sinTheta * (Vrq + tanU * Vrp);
                        V[static_cast<size_t>(r * N + q)] = Vrq + sinTheta * (Vrp - tanU * Vrq);
                    }
                }
            }
        }

        // Extract eigenvalues from diagonal of A
        for (int i = 0; i < N; ++i)
        {
            eigenvalues[static_cast<size_t>(i)] = std::max(1e-20f, A[static_cast<size_t>(i * N + i)]);
        }

        // Compute sqrtC = V * diag(sqrt(eigenvalues)) * V^T
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                float val = 0.0f;
                for (int k = 0; k < N; ++k)
                {
                    val += V[static_cast<size_t>(i * N + k)]
                         * std::sqrt(eigenvalues[static_cast<size_t>(k)])
                         * V[static_cast<size_t>(j * N + k)];
                }
                sqrtC[static_cast<size_t>(i * N + j)] = val;
            }
        }
    }
};

} // namespace transfo
