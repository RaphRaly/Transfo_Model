# TWISTERION — Academic Research Compilation
**Date:** 2026-03-27
**Purpose:** Literature review for the 6 known limitations, with solutions from academic sources.

---

## Table of Contents
1. [Problem 1: HSIM Delay-Free Loop](#1-hsim-delay-free-loop)
2. [Problem 2: JE-990 H2/H3 Harmonic Inversion](#2-je-990-h2h3-harmonic-inversion)
3. [Problem 3: NR Solver Sporadic Divergence](#3-nr-solver-sporadic-divergence)
4. [Problem 4: Frequency-Dependent Saturation](#4-frequency-dependent-saturation-hscale)
5. [Problem 5: Bertotti Dynamic Losses Instability](#5-bertotti-dynamic-losses-instability)
6. [Problem 6: Temperature-Dependent Permeability](#6-temperature-dependent-permeability)

---

## 1. HSIM Delay-Free Loop

### Root Cause
The HSIM solver couples 3 nonlinear reluctances instantaneously through a 9-port MEJunction scattering matrix, creating a same-sample algebraic loop with spectral radius > 1.

### Key Papers

**Werner (2016)** — PhD Dissertation, Stanford CCRMA
"Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters"
URL: https://stacks.stanford.edu/file/druid:jy057cz8322/KurtJamesWernerDissertation-augmented.pdf
- Chapter 5: Multi-dimensional Newton-Raphson for N coupled nonlinearities
- R-type adaptor encapsulates the linear sub-tree; nonlinear elements connect as separate ports
- For 3 unknowns (our case): 3x3 Jacobian, 2-4 iterations/sample

**Werner, Nanez, Smith, Abel (DAFx-15)**
"Resolving Wave Digital Filters with Multiple/Multiport Nonlinearities"
- Introduces R-type adaptor concept for multiple nonlinearities
- Directly applicable to our 9-port junction with 3 nonlinear reluctances

**Werner, Smith, Abel (IEEE TASLP 2018)**
"A General Topology-Independent Framework for Wave Digital Filters"
DOI: 10.1109/TASLP.2017.2772xxx
- MNA or Tableau analysis to derive scattering matrix for any linear sub-circuit
- Removes constraint that WDF must form a binary tree

**Bernardini, Sarti (IEEE SPL 2016)**
"Toward Canonical WDF Realizations of Arbitrary Electrical Networks with Multiple Nonlinearities"
DOI: 10.1109/LSP.2016.2530895
- MNA-based scattering matrix derivation
- Multi-port nonlinear junction with joint Newton solve

**Bernardini, Werner, Smith, Sarti (IEEE TASLP 2019)**
"Generalized Wave Digital Filter Realizations of Arbitrary Reciprocal Connection Networks"
DOI: 10.1109/TASLP.2018.2886170
- Unified R-type + MNA approach
- Symmetry exploitation for reciprocal networks (magnetic circuits qualify)

**Bernardini, Sarti (2017) — Piecewise-Linear**
"Canonical Piecewise-Linear Representation of Curves in the Wave Digital Domain"
- If BH curves approximated as piecewise-linear: implicit solve reduces to region detection + linear system
- No iteration needed — guaranteed convergence

**Giampiccolo, Bernardini, Sarti (JAES 2021)**
"Wave Digital Modeling of Nonlinear 3-Terminal Devices for Virtual Analog Applications"
DOI: 10.17743/jaes.2021.0028
- Multi-terminal nonlinear device as multi-port in WDF
- Companion model approach transferable to multi-port magnetic junction

### Recommended Solutions (ranked)

| # | Approach | Principle | Cost | Convergence |
|---|----------|-----------|------|-------------|
| 1 | Multi-dim Newton 3x3 | Solve F(b)=b-S*a(b)=0 | 2-4 iter/sample | Guaranteed if monotone |
| 2 | Piecewise-linear BH | Segment detection + linear solve | Very fast | Guaranteed |
| 3 | Adaptive port resistance | Z = slope of BH at operating point | Recompute scattering | May recover spectral radius < 1 |
| 4 | Unit delay insertion | Break loop with z^-1 | None | Always stable, loses precision |

---

## 2. JE-990 H2/H3 Harmonic Inversion

### Root Cause
One-port companion-source BJTs in a binary WDF tree introduce unit delays in the feedback loop path. Cumulative phase shift reduces open-loop gain from 125 dB (real) to ~60 dB (model), insufficient to suppress VAS-generated H2.

### Key Papers

**Bernardini, Werner, Sarti, Smith (WASPAA 2015)**
"Multi-Port NonLinearities in Wave Digital Structures"
DOI: 10.1109/WASPAA.2015.7336922
- Two-port BJT: both BE and BC junctions as nonlinear ports
- Internal transistor feedback (beta * Ic) resolved implicitly
- Dramatically increases achievable small-signal gain per stage

**Werner (2016) — PhD Dissertation, Ch.4-6**
- Full Ebers-Moll two-port BJT model in WDF
- Both junctions nonlinear → beta naturally achieved (200-400)
- Emitter degeneration INSIDE the adaptor (not pre-scaled analytically)

**Werner, Smith, Abel (IEEE TCSI 2018)**
"Connections Between Wave Digital and Kirchhoff Domain Circuits"
DOI: 10.1109/TCSI.2017.2758858
- Proves any Kirchhoff circuit (including 125 dB loop gain) representable in WDF
- Requires multi-port junctions (R-type adaptors)

**Bernardini, Sarti (DAFx-17)**
"Wave Digital Filters with Multiple/Multiport Nonlinearities"
- Delay-free feedback loops: group all elements in feedback loop into single R-type adaptor
- If diff pair, VAS, output stage separated by unit-delay adaptors, loop gain drops structurally

**Bernardini, Sarti (IEEE TCSII 2018)**
DOI: 10.1109/TCSII.2017.2703927
- Algorithmic decomposition minimizing implicit equations
- Feedback resistor network embedded linearly in adaptor scattering matrix
- Examples with 4-5 nonlinear ports solved jointly

**Bernardini, Sarti (IEEE TCSI 2023)**
"Symmetries and Wave Digital Filters"
DOI: 10.1109/TCSI.2023.3256390
- H2 cancellation in diff pair preserved IFF Q1/Q2 share the same R-type adaptor
- Unit-delay adaptors between Q1/Q2 break differential symmetry → H2 survives

**Albertini, Bernardini, Sarti (ICASSP 2023)**
"Neural Network-Enhanced Wave Digital Filters for Real-Time Audio Processing"
- Neural network to approximate or warm-start multi-port Newton solve
- Reduces per-sample cost from N iterations to single forward pass

### Recommended Solution

1. Replace binary tree with **single R-type adaptor** grouping all 8 BJTs + feedback network
2. Replace one-port BJTs with **two-port nonlinear BJT models** (BE + BC)
3. Solve resulting **8D to 16D** Newton system per sample
4. Place Q1/Q2 diff pair in **same adaptor** for H2 cancellation
5. Optional: neural network acceleration for real-time

---

## 3. NR Solver Sporadic Divergence

### Root Cause
At very low H (hScale ~ 0.065), the J-A model produces negative susceptibility near reversal points. The NR Jacobian changes sign, warm-start is poor, and the solver diverges on ~1/5000 samples.

### Key Papers

**Zirka, Moroz, Harrison, Chwastek (J. Appl. Phys. 2012)**
"On physical aspects of the Jiles-Atherton hysteresis models"
DOI: 10.1063/1.4747915
URL: https://pubs.aip.org/aip/jap/article/112/4/043916/939438/
- Demonstrates negative susceptibility (dM/dH < 0) is intrinsic to J-A near reversal points
- Traces to coenergy vs energy derivation error
- This is the root cause of our NR divergence at low H

**Zhao, Zhou, Zhang, Sun (Materials 2024)**
"State Space Representation of Jiles-Atherton Hysteresis Model"
DOI: 10.3390/ma17153695
URL: https://pmc.ncbi.nlm.nih.gov/articles/PMC11313527/
- **L'Hopital linearization** when He ~ 0: replace standard formula with analytical linear approximation below threshold Htr
- Direct fix for low-H divergence

**Zhang, Xue (AIMS Mathematics 2024)**
"Numerical analysis of the frequency-dependent Jiles-Atherton hysteresis model"
DOI: 10.3934/math.20241517
URL: https://www.aimspress.com/article/doi/10.3934/math.20241517
- **Trapezoidal + fixed-point iteration** instead of NR → more robust when frequency changes
- No Jacobian needed

**Holters, Zolzer (DAFx-16)**
"Circuit Simulation with Inductors and Transformers Based on the Jiles-Atherton Model"
URL: https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Holters_jamodel_DAFx16.pdf
- Reformulate as **dM/dt instead of dM/dH** → eliminates denominator singularity at small dH

**Guerin et al. (JAE 2016)**
"Using a J-A vector hysteresis model with NR and relaxation procedure"
DOI: 10.3233/JAE-162078
- **Relaxation with residual-norm minimization** → guaranteed descent at every step

**Werner et al. (IEEE Trans. Magn. 2017/2018)**
"Newton-Raphson Solver for FEM with Nonlinear Hysteresis Models"
URL: https://ieeexplore.ieee.org/document/8088356/
- **Wolfe-Powell line search** (Armijo + curvature conditions)
- Improved initial guess evaluation

**Benabou, Clenet, Piriou (JMMM 2003)**
"Comparison of Preisach and Jiles-Atherton models for FE analysis"
DOI: 10.1016/S0304-8853(02)01463-4
- **Bisection fallback** when NR doesn't converge in N iterations
- Hybrid NR/bisection guarantees 100% convergence

**Chowdhury (DAFx-19)**
"Real-Time Physical Modelling for Analog Tape Machines"
URL: https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf
- Reference audio-rate J-A implementation (AnalogTapeModel / ChowTape)
- GitHub: https://github.com/jatinchowdhury18/AnalogTapeModel

**Egger, Engertsberger (arXiv 2025)**
"A semi-smooth Newton method for magnetic field problems with hysteresis"
URL: https://arxiv.org/abs/2506.01499
- Proves global linear convergence with line search for semi-smooth hysteresis

**Szewczyk (Springer 2014)**
"Computational Problems Connected with Jiles-Atherton Model of Magnetic Hysteresis"
DOI: 10.1007/978-3-319-05353-0_27
- RK4 with fixed step outperforms other ODE solvers for J-A
- Floating-point precision matters near zero

**Li, Zheng et al. (Mathematics MDPI 2022)**
"Numerical Solving Method for Jiles-Atherton Model and Influence Analysis of the Initial Magnetic Field"
DOI: 10.3390/math10234431
- Secant method with optimized initial values
- Analysis of initial magnetic field effect on convergence

### Recommended Fixes (ranked by effort)

| # | Fix | Source | Effort | Impact |
|---|-----|--------|--------|--------|
| 1 | L'Hopital linearization near He=0 | Zhao 2024 | 1h | Eliminates singularity |
| 2 | Bisection fallback after N iter | Benabou 2003 | 1h | 100% convergence guarantee |
| 3 | Armijo line-search on NR step | Werner/Guerin 2016-17 | 2h | Prevents overshoot |
| 4 | dM/dt reformulation (not dM/dH) | Holters 2016 | 1-2 days | Structural fix |

---

## 4. Frequency-Dependent Saturation (hScale)

### Root Cause
Fixed `hScale` converts voltage directly to H-field without time integration. Real transformers: B_peak = V/(2*pi*f*N*A), so flux (and saturation) scales as 1/f. Our model misses this.

### Key Papers

**Giampiccolo, Bernardini, Gruosso, Maffezzoni, Sarti (JAES 2021)**
"Multiphysics Modeling of Audio Circuits With Nonlinear Transformers"
URL: https://www.aes.org/e-lib/browse.cfm?elib=21107
- **THE key paper.** Gyrator-capacitor analogy in WDF:
  - Gyrator converts V→dPhi/dt
  - Nonlinear capacitor in magnetic domain accumulates Phi
  - Saturation inherently frequency-dependent (1/f scaling emerges)
- Demonstrated on vacuum-tube guitar amplifier output stage

**Giampiccolo, Bernardini, Gruosso, Maffezzoni, Sarti (IJCTA 2022)**
"Multidomain Modeling of Nonlinear Electromagnetic Circuits Using Wave Digital Filters"
DOI: 10.1002/cta.3146
URL: https://onlinelibrary.wiley.com/doi/full/10.1002/cta.3146
- General framework extending JAES 2021
- Gyrator junction in WDF: electrical port (V,I) ↔ magnetic port (MMF,dPhi/dt)
- No frequency-calibrated scaling factor needed

**Paiva, Pakarinen, Valimaki, Tikander (EURASIP 2011)**
"Real-Time Audio Transformer Emulation for Virtual Tube Amplifiers"
DOI: 10.1155/2011/347645
URL: https://link.springer.com/article/10.1155/2011/347645
- WDF transformer model with gyrator-capacitor analogy
- Confirms distortion at low frequencies only (<100 Hz Fender, <30 Hz Hammond)
- Practical parameter estimation from basic electrical measurements

**Holters, Zolzer (DAFx-16)**
"Circuit Simulation with Inductors and Transformers Based on the Jiles-Atherton Model"
URL: https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Holters_jamodel_DAFx16.pdf
- J-A rewritten as dM/dt ODE coupled with circuit differential equations
- Flux linkage lambda = integral(V dt) / N as coupling variable
- At 20 Hz vs 1 kHz for same voltage: flux excursion 50x larger → more saturation

**Macak, Schimmel (IEEE TSP 2011)**
"Nonlinear Transformer Simulation for Real-Time Digital Audio Signal Processing"
URL: https://ieeexplore.ieee.org/document/6043681/
- Compares J-A and Frohlich models for audio transformers
- Flux = integral of voltage / turns → frequency-dependent saturation captured by integration

**Massi, Mezza, Giampiccolo, Bernardini (EURASIP JASM 2023)**
"Deep Learning-Based Wave Digital Modeling of Rate-Dependent Hysteretic Nonlinearities"
URL: https://link.springer.com/article/10.1186/s13636-023-00277-8
- RNN replaces analytical hysteresis model as WDF one-port
- Naturally captures rate-dependent behavior (frequency-dependent loop widening)

**Sarti, De Poli (IEEE Trans. SP 1999)**
"Toward Nonlinear Wave Digital Filters"
URL: https://ieeexplore.ieee.org/document/765137/
- "Mutators" transform nonlinear inductor into nonlinear resistor in WDF
- Performs voltage-to-flux integration inside WDF structure

**Chen, Xu, Ruan, Wong, Tse (IEEE APEC 2009)**
"Gyrator-Capacitor Simulation Model of Nonlinear Magnetic Core"
URL: https://ieeexplore.ieee.org/document/4802905/
- Circuit-theoretic foundation: gyrator converts between electrical and magnetic domains
- Flux stored as capacitor charge → inherently integral of voltage

**Giampiccolo (PhD Dissertation, Politecnico di Milano 2023)**
"Multiphysics Modeling of Audio Systems in the Wave Digital Domain"
URL: https://www.researchgate.net/publication/379121562
- Most comprehensive and recent treatment
- Full multiphysics pipeline: electrical + magnetic + mechanical + acoustic

### Recommended Solutions

| # | Approach | Principle | Effort |
|---|----------|-----------|--------|
| 1 | **Gyrator WDF** (best) | Gyrator N:1 converts V→dPhi/dt, nonlinear cap for B(H) | 2-3 weeks |
| 2 | **Flux integrator** | Replace H=signal*hScale with Phi+=signal*dt/N; H=f(Phi) | 3-5 days |
| 3 | **hScale freq-aware** | Pre-filter signal with integrator 1/s before J-A | 1-2 days (approx) |

---

## 5. Bertotti Dynamic Losses Instability

### Root Cause
Cascade subtracts H_dyn = K1*dB/dt + K2*sqrt(|dB/dt|)*sign(dB/dt) from H_applied. When H_applied is small (Physical mode, hScale~0.065), H_dyn > H_applied → sign inversion → J-A receives wrong H direction → massive artifacts.

### Key Papers

**Baghel, Kulkarni (IEEE Trans. Magn. 2014)** — THE KEY PAPER
"Dynamic Loss Inclusion in the Jiles-Atherton Hysteresis Model Using the Original JA Approach and the Field Separation Approach"
DOI: 10.1109/TMAG.2013.2284381
URL: https://ieeexplore.ieee.org/document/6749253
- Compares: (A) "original JA" embeds H_dyn in H_eff inside ODE → produces non-physical vertical lengthening (our bug)
- vs (B) "field separation" computes H_stat from inverse static J-A first, then adds H_eddy + H_excess
- Field separation is physically correct, avoids sign inversion

**Zirka, Moroz, Marketos, Moses (Physica B 2004)**
"Dynamic Hysteresis Modelling"
DOI: 10.1016/j.physb.2003.08.036
URL: https://www.sciencedirect.com/science/article/abs/pii/S092145260300557X
- Viscous-type dynamic model: models time lag of B behind H as differential equation
- Completely sidesteps sign-inversion problem
- Compatible with any static model (J-A, Preisach, etc.)

**Zirka, Moroz, Marketos, Moses (IEEE Trans. Magn. 2006)**
"Viscosity-Based Magnetodynamic Model of Soft Magnetic Materials"
DOI: 10.1109/TMAG.2006.880685
URL: https://ieeexplore.ieee.org/document/1678055/
- Full development with numerical scheme
- Excess loss derived from Landau-Lifshitz-Gilbert equation
- Modeling errors ~1% for tested materials

**Zirka, Moroz, Chiesa, Harrison, Hoidalen (IEEE TPWRD 2015)**
"Implementation of Inverse Hysteresis Model into EMTP — Part II: Dynamic Model"
DOI: 10.1109/TPWRD.2015.2416199
URL: https://ieeexplore.ieee.org/document/7086071/
- Practical EMTP implementation of viscous DHM
- Inverse (B-driven) formulation: H = output given B = input
- Inherently avoids sign inversion

**Podbereznaya, Pavlenko (JMMM 2020)**
"Accounting for Dynamic Losses in the Jiles-Atherton Model"
DOI: 10.1016/j.jmmm.2020.167070
URL: https://www.sciencedirect.com/science/article/abs/pii/S0304885320306107
- Conventional dynamic J-A "cannot be explained from a physical point of view"
- Field-separation formulation avoids instability

**Aidel, Hamimid (I2M 2023)**
"Improving the Swelling Phenomenon in the Dynamic J-A Hysteresis Model Using Magnetic Viscosity"
DOI: 10.18280/i2m.220405
URL: https://www.iieta.org/journals/i2m/paper/10.18280/i2m.220405
- Directly addresses "swelling phenomenon" = our sign-inversion problem
- Variable excess field parameter alpha_exc(B(t)) eliminates need for global peak detection
- Stable sample-by-sample

**Ducharne, Sebald, Guyomar (IEEE CISTEM 2018)**
"Dynamic Magnetic Scalar Hysteresis Lump Model Based on J-A Extended with Fractional Derivatives"
DOI: 10.1109/CISTEM.2018.8508104
URL: https://ieeexplore.ieee.org/document/8508104/
- Single fractional derivative d^n(B)/dt^n (0 < n < 1) replaces both dB/dt and sqrt(|dB/dt|) terms
- Smooth at zero crossings → no sign instability
- Accurate over multiple frequency decades

**Ducharne, Ragusa, Fiorillo (EPJ Plus 2020)**
"Anomalous Fractional Diffusion Equation for Magnetic Losses in a Ferromagnetic Lamination"
DOI: 10.1140/epjp/s13360-020-00330-x
URL: https://link.springer.com/article/10.1140/epjp/s13360-020-00330-x
- Fractional diffusion replaces classical diffusion → captures both classical and excess losses
- Single fractional-order PDE eliminates three-term decomposition

**Mousavi, Engdahl (IET CEM 2014)**
"Modelling Static and Dynamic Hysteresis in Time Domain Reluctance Networks"
DOI: 10.1049/CP.2014.0208
- Linearize dynamic model locally: H^N = K1*B^N - K2
- Dynamic loss as local linear correction → no global subtraction → no sign inversion

**Bertotti (IEEE Trans. Magn. 1988)** — Original reference
"General Properties of Power Losses in Soft Ferromagnetic Materials"
DOI: 10.1109/20.43994
URL: https://ieeexplore.ieee.org/document/43994/

### Recommended Solutions (ranked)

| # | Approach | Principle | Stability | Effort |
|---|----------|-----------|-----------|--------|
| 1 | **Inverse field-separation** | B→input, H=H_stat+H_dyn→output | Perfect | 1 week |
| 2 | **Viscosity (Zirka-Moroz)** | Viscous time-lag instead of field subtraction | Perfect | 1-2 weeks |
| 3 | **Local linearization** | H=K1*B-K2 each sample | Good | 2-3 days |
| 4 | **Fractional derivative** | d^n(B)/dt^n, 0<n<1 | Good | 2-3 weeks |

---

## 6. Temperature-Dependent Permeability

### Root Cause
All J-A parameters (Ms, a, k, c, alpha) are fixed at room temperature. No warmup behavior modeled.

### Key Papers with Numerical Data

**Sun, Ren, Li, Huang (Materials 2023)**
"Measurement and Analysis of Magnetic Properties of Permalloy for Magnetic Shielding Devices under Different Temperature Environments"
DOI: 10.3390/ma16083253
URL: https://pmc.ncbi.nlm.nih.gov/articles/PMC10145743/
Material: 1J85 permalloy (Ni 79-81%, Mo 4.8-5.2%)

MEASURED DATA (-60C to +140C relative to 25C):
| Property          | -60C     | +140C    | Trend                  |
|-------------------|----------|----------|------------------------|
| mu_initial        | -69.6%   | +38.2%   | Exponential increase   |
| mu_max            | -38.2%   | -25.4%   | Peaks then declines    |
| Bs                | +5.9%    | -11.0%   | Linear decrease        |
| Hc (coercivity)   | +84.6%   | -22.9%   | Exponential decay      |
| Br (remanence)    | -34.8%   | +8.9%    | Exponential increase   |
| mu_AC @ 1kHz      | -12.3%   | +9.4%    | Smaller variation at HF|

**Li et al. (AIP Advances 2025)**
"Magnetic behavior of high permeability materials over wide temperature range"
DOI: 10.1063/9.0000932
URL: https://pubs.aip.org/aip/adv/article/15/3/035224/3339635/
Material: 1J50 (50% NiFe), 73K to 473K, 3-800 Hz
- At low flux: permeability increases with temperature
- Near saturation (~0.7T): permeability peaks then decreases

**Raghunathan, Melikhov, Snyder, Jiles (IEEE Trans. Magn. 2009)** — J-A TEMPERATURE MODEL
"Modeling the Temperature Dependence of Hysteresis Based on Jiles-Atherton Theory"
DOI: 10.1109/TMAG.2009.2022744
URL: https://ieeexplore.ieee.org/abstract/document/5257370/

Temperature-dependent J-A parameters:
```
Ms(T) = Ms0 * (1 - T/Tc)^beta       [beta ~ 0.35-0.50]
a(T)  proportional to kB*T / (mu0*m) [increases linearly with T]
k(T)  proportional to Ms(T)          [decreases with T]
alpha(T) proportional to Ms(T)       [decreases with T]
c     increases with T
```
Two additional parameters: Tc (Curie temperature), beta (critical exponent)

**Hussain, Benabou, Clenet, Lowther (IEEE Trans. Magn. 2018)**
"Temperature Dependence in the J-A Model for Non-Oriented Electrical Steels"
DOI: 10.1109/TMAG.2018.2837126
URL: https://hal.science/hal-01858668
- Simplified identification without knowing Tc

**Raghunathan, Melikhov, Snyder, Jiles (IEEE Trans. Magn. 2010)**
"Theoretical Model of Temperature Dependence of Hysteresis Based on Mean Field Theory"
URL: https://www.researchgate.net/publication/224139318
- Theoretical justification from mean-field theory

**Bozorth (1951)**
"Ferromagnetism" — IEEE Press reprint 1993
URL: https://sti.passtek.net/livres/References/Ferromagnetism.pdf
- Definitive reference for NiFe permeability vs temperature charts

**Sgobba (CERN 2011)**
"Physics and measurements of magnetic materials"
URL: https://arxiv.org/abs/1103.1069
- NiFe properties for particle accelerator shielding applications

**Perin et al. (JMMM 2020)**
"Evolution and recent developments of 80%Ni permalloys"
URL: https://www.sciencedirect.com/science/article/abs/pii/S0304885319327337

### Compiled Material Properties

#### 80% NiFe (Mu-metal) — used in JT-115K-E
| Property | Value |
|----------|-------|
| Composition | Ni 79-81%, Mo 3.8-5.2%, Fe balance |
| Curie Temp (Tc) | 420-460 C (693-733 K) |
| Bs (25C) | 0.73-0.80 T |
| mu_initial | 50,000 - 550,000 |
| mu_max | 200,000 - 570,000 |
| Hc | 0.008-0.02 Oe (0.64-1.6 A/m) |
| Resistivity | 55-60 uOhm*cm |
| Specific heat | 0.494 J/g/K |
| Thermal conductivity | 34.6 W/m/K |
| Bs derating @ 125C | -8 to -9% |
| mu variation (studio range 20-85C) | ~0.3%/C |

#### 50% NiFe — used in JT-11ELCF
| Property | Value |
|----------|-------|
| Composition | Ni 47-50%, Fe balance |
| Curie Temp (Tc) | 450 C (723 K) |
| Bs (25C) | 1.5-1.6 T |
| mu_initial | 6,500-12,000 |
| mu_max | 75,000-190,000 |
| Hc | 0.04-0.07 Oe |
| Resistivity | 45-48 uOhm*cm |
| Bs derating @ 125C | -9 to -11% |

### Implementation Model

For the J-A temperature extension (Raghunathan 2009):
```cpp
// T in Kelvin, Tc in Kelvin
double Ms_T   = Ms_0 * pow(1.0 - T / Tc, beta);  // beta ~ 0.4
double a_T    = a_0 * (T / T_ref);                 // T_ref = 298K
double k_T    = k_0 * (Ms_T / Ms_0);
double alpha_T = alpha_0 * (Ms_T / Ms_0);
double c_T    = c_0 * (T_ref / T);                 // approximation
```

Thermal RC model for core warmup:
```
dT/dt = (P_loss - (T - T_ambient) / R_thermal) / C_thermal

C_thermal = mass * specific_heat   [J/K]
R_thermal = 1 / (h * A_surface)    [K/W]  (h = convection coefficient)
```

Typical audio transformer core warmup: tau = R*C ~ 5-15 minutes.

---

## Priority Matrix

| Problem | Impact | Fix Effort | Recommended Paper |
|---------|--------|------------|-------------------|
| #4 hScale (freq-dep sat) | HIGH | 3-5 days (flux integrator) | Giampiccolo JAES 2021 |
| #3 NR spikes | MEDIUM | 1-2 hours (L'Hopital + bisection) | Zhao 2024 + Benabou 2003 |
| #5 Bertotti instability | MEDIUM | 2-3 days (local linearization) | Baghel & Kulkarni 2014 |
| #2 H2/H3 inversion | HIGH | 2-3 weeks (R-type adaptor) | Bernardini DAFx-17 |
| #1 HSIM divergence | HIGH | 2-3 weeks (Newton 3x3) | Werner PhD 2016 Ch.5 |
| #6 Temperature | LOW | 1 week | Raghunathan 2009 |

---

*Compiled 2026-03-27 from 6 parallel literature searches (~200 web queries total).*
*Sources verified via WebSearch + WebFetch where possible.*
