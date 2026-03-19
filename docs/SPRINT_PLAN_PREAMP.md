# Sprint Plan: Dual-Topology Discrete Preamp Model

**Project**: Transformer Model — Preamp Extension
**Date**: 2026-03-18
**Spec Source**: `ANALYSE_ET_DESIGN_Rev2.md` (Rev 2.0, Dual Topology B+C)
**Overview**: `PREAMP_OVERVIEW.md`
**Total effort**: 8 sprints (S0–S7)
**Architecture**: WDF-hybrid with Strategy pattern for A/B path switching

---

## Product Vision

Modeliser le preampli micro discret dual-topology decrit dans l'ANALYSE_ET_DESIGN_Rev2.md :
un circuit complet **JT-115K-E → [Neve Class-A | JE-990] → JT-11ELCF** avec
les deux chemins d'amplification commutables en temps reel.

Le modele DSP capture les interactions non-lineaires entre transformateurs et
electronique active : impedance source, saturation magnetique niveau-dependante,
distorsion de transfert des transistors, et resonances LC parasites.

### User Stories (Epic)

| ID | En tant que... | Je veux... | Pour... |
|----|----------------|------------|---------|
| US-1 | Utilisateur du plugin | Selectionner Chemin A ou B en temps reel | Comparer le caractere Neve vs Jensen |
| US-2 | Utilisateur | Controler le gain de +23 a +69 dB par crans | Reproduire le comportement du Grayhill |
| US-3 | Utilisateur | Activer/desactiver le PAD | Entendre l'interaction impedance-source/transfo |
| US-4 | Utilisateur | Switcher le ratio 1:5 / 1:10 | Choisir entre precision et coloration |
| US-5 | Ingenieur DSP | Voir les courbes I-V des BJT en temps reel | Valider le point de fonctionnement |
| US-6 | Ingenieur DSP | Mode Physical (J-A) et Realtime (CPWL) | Offline precision vs monitoring temps reel |

---

## Architecture Overview

### Signal Flow

```
                         +48V Phantom
                              |
                       [6.81K x2 0.1%]
                              |
MIC XLR ──[649Ω PAD]──── JT-115K-E (T1)
                              |
                       [13.7K + 680pF]
                              |
              ┌───────────────┴───────────────────┐
              |                                   |
     ═══ CHEMIN A ═══                    ═══ CHEMIN B ═══
     NeveClassAPath                      JE990Path
     (Strategy)                          (Strategy)
     ┌─────────────────┐                 ┌─────────────────┐
     │ Q1 BC184C  (CE) │                 │ LM-394 diff pair│
     │ Q2 BC214C  (CE) │                 │ PNP cascode     │
     │ Q3 BD139   (EF) │                 │ VAS + Miller    │
     │ Feedback + Gain  │                 │ Class-AB output │
     └────────┬────────┘                 │ L1,L2 emitters  │
              |                          │ L3 load isolator│
         [C_out + 10Ω]                  └────────┬────────┘
              |                                   |
              |                          [C_out + 39Ω + L3]
              |                                   |
              └──────────┐    ┌───────────────────┘
                         |    |
                    [ A/B Crossfade ]
                         |
                    JT-11ELCF (T2)
                         |
                    XLR OUT (balanced)
```

### Class Architecture

```
PreampModel<NonlinearLeaf>                    [Template Method]
├── InputStageWDF                             [Composite]
│   ├── PhantomNetwork (R+R)                  [WDF Adapted Elements]
│   ├── PadNetwork (649Ω series)              [WDF Adapted Elements]
│   ├── TransformerCircuitWDF<NL> (T1)        [Existing, reused]
│   └── TerminationNetwork (13.7K + 680pF)    [WDF Parallel]
│
├── IAmplifierPath                            [Strategy Interface]
│   ├── NeveClassAPath                        [Concrete Strategy A]
│   │   ├── CEStageWDF (Q1 BC184C)            [WDF sub-circuit]
│   │   ├── CEStageWDF (Q2 BC214C)            [WDF sub-circuit]
│   │   ├── EFStageWDF (Q3 BD139)             [WDF sub-circuit]
│   │   └── FeedbackNetwork                   [Gain-dependent]
│   │
│   └── JE990Path                             [Concrete Strategy B]
│       ├── DiffPairWDF (LM-394 + L1,L2)      [WDF sub-circuit]
│       ├── CascodeStageWDF (Q3,Q5,Q6)        [Linearized WDF]
│       ├── VASStageWDF (Q6 + C1=150pF)        [WDF sub-circuit]
│       ├── ClassABOutputWDF (Q8/MJE171)       [WDF sub-circuit]
│       ├── LoadIsolator (39Ω + L3=40µH)       [WDF Series]
│       └── FeedbackNetwork                    [Gain-dependent]
│
├── OutputStageWDF                             [Composite]
│   ├── CouplingCapacitor (220µF + 4.7µF)      [WDF Adapted Cap]
│   ├── ABCrossfade                            [Parametric blend]
│   └── TransformerCircuitWDF<NL> (T2)         [Existing, reused]
│
├── GainController                             [11 positions]
│   ├── gainTable_[11] (Rfb values)
│   └── SmoothedValue<float> currentGain_
│
└── PreampConfig                               [Immutable config]
    ├── InputStageConfig
    ├── NevePathConfig / JE990PathConfig
    └── OutputStageConfig
```

### Design Patterns

| Pattern | Application | Justification |
|---------|-------------|---------------|
| **Strategy** | `IAmplifierPath` → `NeveClassAPath`, `JE990Path` | Commutation A/B sans modifier le pipeline |
| **Template Method** | `PreampModel::processBlock()` | Structure commune, specialisation par chemin |
| **CRTP** | `WDOnePort<BJTLeaf>`, `WDOnePort<DiodeLeaf>` | Zero-overhead dispatch pour les WDF elements (existant) |
| **Composite** | `InputStageWDF`, `OutputStageWDF` | Assemblage de WDF trees en sous-circuits |
| **Pipeline** | Input → Amp → Output | Chaine de traitement lineaire avec isolation des stages |
| **Factory Method** | `PreampConfig::DualTopology()` | Construction de configurations complexes |
| **Observer** | `GainController` → paths, `PadSwitch` → input | Propagation des changements de parametres |
| **Builder** | `PreampCircuitBuilder` | Construction step-by-step des arbres WDF |
| **Dirty Flag** | Impedance adaptation (existant) | Recalcul paresseux des matrices de diffusion |

---

## Sprint 0 — Foundation: Data Structures & Interfaces

**Goal**: Definir toutes les structures de donnees, configs et interfaces abstraites.
Zero changement dans le chemin audio.

**Sprint Duration**: 1 semaine

### User Stories

- US-0.1: En tant que developpeur, je veux une config complete du preamp pour
  instancier le modele avec n'importe quelle combinaison de parametres.
- US-0.2: En tant que developpeur, je veux une interface Strategy pour les chemins
  A/B afin de pouvoir les implementer independamment.

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S0-1 | Creer `PreampConfig.h` — config complete du preamp | `core/include/core/model/PreampConfig.h` | NEW | M | P0 |
| S0-2 | Creer `BJTParams.h` — parametres Ebers-Moll BJT | `core/include/core/model/BJTParams.h` | NEW | S | P0 |
| S0-3 | Creer `IAmplifierPath.h` — interface Strategy | `core/include/core/preamp/IAmplifierPath.h` | NEW | S | P0 |
| S0-4 | Creer `InputStageConfig` dans PreampConfig | `PreampConfig.h` | INCL | S | P0 |
| S0-5 | Creer `NevePathConfig` + `JE990PathConfig` | `PreampConfig.h` | INCL | M | P0 |
| S0-6 | Creer `GainTable.h` — 11 positions Rfb + calcul gain | `core/include/core/preamp/GainTable.h` | NEW | S | P1 |
| S0-7 | Factory: `PreampConfig::DualTopology()` | `PreampConfig.h` | INCL | M | P1 |
| S0-8 | Etendre CMakeLists.txt avec nouveau dossier `preamp/` | `CMakeLists.txt` | MODIFY | S | P0 |

### Structures cles

```cpp
// === BJTParams.h ===
struct BJTParams {
    float Is = 1e-14f;      // Saturation current [A]
    float Bf = 200.0f;      // Forward current gain (beta)
    float Br = 1.0f;        // Reverse current gain
    float Vt = 0.026f;      // Thermal voltage kT/q [V] (25°C)
    float Vaf = 100.0f;     // Early voltage [V] (output impedance)
    float Rb = 10.0f;       // Base resistance [Ω]
    float Rc = 1.0f;        // Collector resistance [Ω]
    float Re = 0.5f;        // Emitter resistance [Ω]
    float Cje = 10e-12f;    // BE junction capacitance [F]
    float Cjc = 5e-12f;     // BC junction capacitance [F]
    enum class Polarity { NPN, PNP } polarity = Polarity::NPN;

    bool isValid() const;

    // Factory presets from ANALYSE_ET_DESIGN_Rev2.md
    static BJTParams BC184C();   // NPN, Chemin A Q1
    static BJTParams BC214C();   // PNP, Chemin A Q2
    static BJTParams BD139();    // NPN power, Chemin A Q3
    static BJTParams LM394();    // NPN matched pair, Chemin B input
    static BJTParams N2N4250A(); // PNP, Chemin B cascode
    static BJTParams N2N2484();  // NPN, Chemin B tail/pre-driver
    static BJTParams MJE181();   // NPN power, Chemin B output top
    static BJTParams MJE171();   // PNP power, Chemin B output bottom
};

// === IAmplifierPath.h ===
class IAmplifierPath {
public:
    virtual ~IAmplifierPath() = default;
    virtual void prepare(float sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual float processSample(float input) = 0;
    virtual void processBlock(const float* input, float* output, int numSamples) = 0;
    virtual void setGain(float Rfb) = 0;           // From GainTable
    virtual float getOutputImpedance() const = 0;   // For T2 loading
    virtual const char* getName() const = 0;
};

// === PreampConfig.h ===
struct InputStageConfig {
    bool phantomEnabled = true;
    float R_phantom = 6810.0f;      // [Ω] 0.1%
    bool padEnabled = false;
    float R_pad = 649.0f;           // [Ω] SSL-spec
    float R_termination = 13700.0f; // [Ω]
    float C_termination = 680e-12f; // [F] Jensen-spec (100pF SSL alt.)
    enum class Ratio { X5, X10 } ratio = Ratio::X10;
    TransformerConfig t1Config;     // JT-115K-E
};

struct NevePathConfig {
    BJTParams q1 = BJTParams::BC184C();
    BJTParams q2 = BJTParams::BC214C();
    BJTParams q3 = BJTParams::BD139();
    float R_bias_q3 = 390.0f;      // [Ω] emitter follower bias
    float C_miller = 100e-12f;     // [F] compensation
    float C_input = 100e-6f;       // [F] input coupling
    float C_out = 220e-6f;         // [F] output coupling (+ 4.7µF film)
    float R_series_out = 10.0f;    // [Ω] before T2
    float Vcc = 24.0f;             // [V] supply
};

struct JE990PathConfig {
    BJTParams q1q2 = BJTParams::LM394();  // Matched pair
    BJTParams q3_cascode = BJTParams::N2N4250A();
    BJTParams q4_tail = BJTParams::N2N2484();
    BJTParams q5_cascode = BJTParams::N2N4250A();
    BJTParams q6_vas = BJTParams::N2N4250A();
    BJTParams q7_predriver = BJTParams::N2N2484();
    BJTParams q8_top = BJTParams::MJE181();
    BJTParams q9_bottom = BJTParams::MJE171();
    float L1 = 20e-6f;             // [H] emitter inductance Q1
    float L2 = 20e-6f;             // [H] emitter inductance Q2
    float L3 = 40e-6f;             // [H] output isolator
    float R_load_isolator = 39.0f; // [Ω] Allen Bradley
    float C1_miller = 150e-12f;    // [F] main Miller comp
    float C2_comp = 62e-12f;       // [F] output comp top
    float C3_comp = 91e-12f;       // [F] output comp bottom
    float C_out = 220e-6f;         // [F] output coupling
    float Vcc = 24.0f;             // [V] supply
};

struct PreampConfig {
    std::string name;
    InputStageConfig input;
    NevePathConfig neveConfig;
    JE990PathConfig je990Config;
    TransformerConfig t2Config;     // JT-11ELCF
    float Rg = 47.0f;              // [Ω] gain reference resistor

    static PreampConfig DualTopology();  // Default dual config
    bool isValid() const;
};
```

### GainTable (11 positions)

```cpp
struct GainTable {
    static constexpr int kNumPositions = 11;
    static constexpr float kRg = 47.0f;  // Reference resistor

    // Rfb values from ANALYSE_ET_DESIGN_Rev2.md (E96, 1%)
    static constexpr std::array<float, kNumPositions> kRfb = {
        100.0f,   // +10 dB
        187.0f,   // +14 dB
        324.0f,   // +18 dB
        536.0f,   // +22 dB
        887.0f,   // +26 dB
        1430.0f,  // +30 dB
        2320.0f,  // +34 dB
        3650.0f,  // +38 dB
        5900.0f,  // +42 dB
        9310.0f,  // +46 dB
        14700.0f  // +50 dB
    };

    static float getGainLinear(int position);  // 1 + Rfb/Rg
    static float getGainDB(int position);      // 20*log10(...)
    static float getRfb(int position);
};
```

### Acceptance Criteria (Definition of Done)

- [ ] `PreampConfig::DualTopology()` compile et retourne une config valide
- [ ] `PreampConfig::isValid()` passe pour la config par defaut
- [ ] `BJTParams::BC184C()` etc. retournent des parametres physiquement plausibles
- [ ] `GainTable::getGainDB(0)` == 10.0 ±0.1 dB
- [ ] `GainTable::getGainDB(10)` == 50.0 ±0.1 dB
- [ ] `IAmplifierPath` compiles comme interface pure (= 0)
- [ ] Tous les tests existants passent (zero regression)

---

## Sprint 1 — BJT & Diode WDF Nonlinear Elements

**Goal**: Construire les elements WDF non-lineaires fondamentaux : BJT (Ebers-Moll)
et diode. Ce sont les briques de base pour les deux chemins d'amplification.

**Sprint Duration**: 2 semaines

### Approche technique : BJT en WDF

Le BJT est decompose en **deux jonctions diodes couplees** (modele Ebers-Moll) :

```
              Ic
     C ───────┤
              │
         ┌────┤ Ibc (diode BC, reverse)
         │    │
    B ───┤    │
         │    │
         └────┤ Ibe (diode BE, forward)
              │
     E ───────┘
              Ie

Ebers-Moll equations:
    Ibe = Is/Bf * (exp(Vbe/Vt) - 1)
    Ibc = Is/Br * (exp(Vbc/Vt) - 1)
    Ic  = Bf*Ibe - Ibc
    Ie  = -(1+Bf)*Ibe + (1+Br)*Ibc
```

**Strategie WDF** : Modeliser chaque jonction comme un **WDOnePort non-lineaire**
resolu par Newton-Raphson, similaire au JilesAthertonLeaf. Le couplage entre
les deux jonctions est gere par mise a jour iterative des sources compagnons.

Pour le Chemin A (3 transistors), chaque etage est un sous-circuit WDF
avec 1 element non-lineaire (la jonction active BE en mode forward-active).
En forward-active, Vbc < 0, donc la jonction BC est en inverse → linearisable.
Cela simplifie le BJT a un **one-port diode + source de courant controlee**.

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S1-1 | Creer `DiodeLeaf.h` — diode WDF non-lineaire | `core/include/core/wdf/DiodeLeaf.h` | NEW | M | P0 |
| S1-2 | Creer `BJTLeaf.h` — transistor WDF (Ebers-Moll simplifie) | `core/include/core/wdf/BJTLeaf.h` | NEW | L | P0 |
| S1-3 | Creer `BJTCompanionModel.h` — modele companion pour NR | `core/include/core/wdf/BJTCompanionModel.h` | NEW | L | P0 |
| S1-4 | Test: DiodeLeaf I-V curve validation | `Tests/test_diode_leaf.cpp` | NEW | M | P0 |
| S1-5 | Test: BJTLeaf characteristic curves (Ic vs Vce family) | `Tests/test_bjt_leaf.cpp` | NEW | L | P0 |
| S1-6 | Test: BJTLeaf polarity NPN/PNP symmetry | `Tests/test_bjt_leaf.cpp` | EXTEND | M | P1 |
| S1-7 | Register tests in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### DiodeLeaf Design

```cpp
// Shockley diode as WDOnePort
class DiodeLeaf : public WDOnePort<DiodeLeaf> {
public:
    struct Params {
        float Is = 1e-14f;    // Saturation current
        float Vt = 0.026f;    // Thermal voltage
        float N  = 1.0f;      // Ideality factor
    };

    void configure(const Params& p);
    void prepare(float sampleRate);
    void reset();

    // WDOnePort CRTP implementation
    float scatterImpl(float a_incident);
    float getPortResistanceImpl() const;

private:
    // Newton-Raphson: solve I_d = Is*(exp(Vd/(N*Vt)) - 1)
    // In wave domain: b = a - 2*Z*I_d(V) where V = (a+b)/2
    // → implicit equation solved per sample
    float solveNewtonRaphson(float a);
    float V_prev_ = 0.0f;  // warm-start from previous solution
};
```

### BJTLeaf Design

```cpp
// Simplified BJT for WDF: forward-active region
// Models BE junction as nonlinear + collector current as companion source
class BJTLeaf : public WDOnePort<BJTLeaf> {
public:
    void configure(const BJTParams& params);
    void prepare(float sampleRate);
    void reset();

    // Main WDF interface (base-emitter port)
    float scatterImpl(float a_incident);
    float getPortResistanceImpl() const;

    // Collector current output (companion source, updated per sample)
    float getCollectorCurrent() const;
    float getCollectorVoltage() const;

    // Operating point monitoring
    float getVbe() const;
    float getIc() const;
    float getGm() const;  // transconductance at operating point

    // Dynamic impedance (for adaptor updates)
    float getSmallSignalRbe() const;  // rbe = Vt / Ib
    float getSmallSignalRce() const;  // rce = Vaf / Ic

private:
    BJTParams params_;
    float Vbe_prev_ = 0.6f;
    float Ic_last_ = 0.0f;
    float gm_ = 0.0f;
};
```

### Acceptance Criteria

- [ ] DiodeLeaf: I-V curve matches Shockley equation (Is=1e-14, Vt=26mV)
  within 1% for V = 0.3..0.8V
- [ ] DiodeLeaf: Newton-Raphson converge en < 5 iterations pour 99% des cas
- [ ] DiodeLeaf: Pas de NaN/Inf pour tout input dans [-50V, +50V]
- [ ] BJTLeaf: Ic = β × Ib dans la region forward-active (Vce > 0.3V)
- [ ] BJTLeaf: Gm = Ic/Vt au point de fonctionnement
- [ ] BJTLeaf: NPN et PNP produisent des courbes symetriques (signe inverse)
- [ ] BJTLeaf: BC184C params → Ic ≈ 1mA pour Vbe ≈ 0.6V, β ≈ 400
- [ ] BJTLeaf: Passivite respectee (energie dissipee ≥ 0)
- [ ] Tous les tests existants passent

---

## Sprint 2 — Input Stage WDF Circuit

**Goal**: Modeliser le circuit d'entree complet : phantom → pad → JT-115K-E
avec terminaison 13.7K + 680pF. Le signal est pret a attaquer les amplificateurs.

**Sprint Duration**: 1.5 semaines

### Schema du circuit

```
                         +48V
                          |
                   R1[6.81K] R2[6.81K]     (phantom, modele comme Zs_eff)
                          |      |
MIC (Vs, Zs) ──[R_pad?]──┴──────┴──── JT-115K-E Primary (T1)
                                            |
                                       T1 Secondary
                                            |
                                   ┌────────┼────────┐
                                   |        |        |
                             R_t[13.7K]  [680pF]   signal → ampli
                                   |        |
                                  GND      GND

WDF Model (referred to secondary):
    AdaptedRSource(Vs_mic, Zs_eff) → TransformerCircuitWDF(T1) → TerminationParallel
                                                                      |
                                                                 R_term || C_term
```

### Impedance source effective (from ANALYSE_ET_DESIGN_Rev2.md §2.3)

```
Zs_eff = Zmic || (R_phantom1 + R_phantom2) + R_pad(si active)

Sans pad:  Zs_eff ≈ 150 || 13620 ≈ 148 Ω  (SM57)
Avec pad:  Zs_eff ≈ 148 + 1298 ≈ 1446 Ω   (SM57 + 649Ω x2)
```

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S2-1 | Creer `InputStageWDF.h` — circuit d'entree complet | `core/include/core/preamp/InputStageWDF.h` | NEW | L | P0 |
| S2-2 | Calcul Zs_eff (phantom + pad + Zmic) | `InputStageWDF.h` | INCL | M | P0 |
| S2-3 | Reseau de terminaison (R_term \|\| C_term) en WDF | `InputStageWDF.h` | INCL | M | P0 |
| S2-4 | Integration T1 via TransformerCircuitWDF existant | `InputStageWDF.h` | INCL | M | P0 |
| S2-5 | Runtime switch: pad ON/OFF, ratio 1:5/1:10 | `InputStageWDF.h` | INCL | M | P1 |
| S2-6 | Test: reponse frequentielle T1 + terminaison | `Tests/test_input_stage.cpp` | NEW | L | P0 |
| S2-7 | Test: Zs_eff avec/sans pad, differents micros | `Tests/test_input_stage.cpp` | EXTEND | M | P1 |
| S2-8 | Register test in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### InputStageWDF Design

```cpp
class InputStageWDF {
public:
    void prepare(float sampleRate, const InputStageConfig& config);
    void reset();

    // Runtime controls
    void setPadEnabled(bool enabled);
    void setRatio(InputStageConfig::Ratio ratio);
    void setSourceImpedance(float Zmic);  // 150Ω (SM57), 200Ω (U87), etc.

    // Processing
    float processSample(float micSignal);
    void processBlock(const float* in, float* out, int numSamples);

    // Monitoring
    float getSecondaryVoltage() const;
    float getMagnetizingCurrent() const;  // From T1 JA model
    float getEffectiveSourceZ() const;

private:
    // WDF components
    AdaptedRSource source_;              // Mic + phantom + pad equivalent
    TransformerCircuitWDF<...> t1_;      // JT-115K-E model (existing)
    AdaptedResistor rTerm_;              // 13.7K termination
    AdaptedCapacitor cTerm_;             // 680pF amortissement
    DynamicParallelAdaptor<2> pTerm_;    // R_term || C_term

    // Config
    InputStageConfig config_;
    float Zs_eff_ = 150.0f;

    void recalculateSourceImpedance();
};
```

### Acceptance Criteria

- [ ] Sans pad, Zs_eff = 148Ω pour Zmic=150Ω → gain T1 = +19.8 dB (1:10)
- [ ] Avec pad, Zs_eff = 1446Ω pour Zmic=150Ω → gain T1 reduit (~-5 dB)
- [ ] Terminaison 13.7K amortit le pic HF resonance T1 (pas de peak >+1dB dans 20-20kHz)
- [ ] Ratio 1:5 → gain -6dB par rapport au 1:10
- [ ] La reponse LF depend du pad (plus de coloration avec pad ON, §2.3)
- [ ] processBlock et processSample produisent des resultats identiques
- [ ] Pas de NaN/Inf pour entrees dans [-1V, +1V] au primaire
- [ ] Tests existants passent

---

## Sprint 3 — Chemin A: Neve Heritage Class-A

**Goal**: Implementer l'amplificateur 3 transistors Classe-A (Option B du Rev 2.0).
Q1 (BC184C CE) → Q2 (BC214C CE) → Q3 (BD139 EF) avec feedback et gain variable.

**Sprint Duration**: 2.5 semaines

### Schema du circuit (de ANALYSE_ET_DESIGN_Rev2.md §3.2)

```
                              +24V
                               |
                    R11[7.5K]──┤ (Q2 Re vers +24V)
                               |E
                          Q2 (BC214C PNP)
                             |B────────────────┐
                             |C                 |
                              |                  |
                         R12[6.8K]          C5[100pF] Miller
                              |                  |
                             -24V                |
Signal T1sec                                     |
     |                                           |
 C3A[100µF]                                      |
     |                                           |
 R6A[100K]──┬── Q1 Base                         |
     |      |  (BC184C NPN)                      |
 R7A[100K]──┘    |C──────────────────────────────┘
     |           |E
    GND          |
            Node E ←── Pole A du GAIN SWITCH (via C6 470µF)
                 |
            R9[47Ω]
                 |
            C4[100µF]   R10[15K]
                 |          |
                GND        -24V

                 +24V ← Q3 Collector
                          |
                     Q3 (BD139 NPN EF)
                          |B ← Q2 Collector
                          |E
                          |
                ┌─────────┤
                |         |
           R_bias[390Ω]  C_out[220µF+4.7µF]
                |         |
              -24V    [10Ω] → vers T2
```

### Decomposition en etages WDF

**Etage 1 — Q1 CE (BC184C NPN)** :
```
WDF sub-circuit:
    Input coupling (C3A=100µF) →
    Series(R_bias_base, BJTLeaf_Q1_BE) →
    Parallel(R_collector_load, C_miller)
    Collector output → Q2 base
```

**Etage 2 — Q2 CE (BC214C PNP)** :
```
WDF sub-circuit:
    Q1_collector → Q2_base →
    BJTLeaf_Q2_BE →
    Series(R_emitter=7.5K to +24V, R_collector=6.8K to -24V)
    Collector output → Q3 base
```

**Etage 3 — Q3 EF (BD139 NPN)** :
```
WDF sub-circuit:
    Q2_collector → Q3_base →
    BJTLeaf_Q3_BE (emitter follower: gain ≈ 1) →
    Parallel(R_bias=390Ω, C_out series)
    Emitter output → feedback + T2
```

**Feedback Network** :
```
Output (Q3 emitter) → C6[470µF] → Rfb[variable] → Q1 emitter (Node E)
                                                         |
                                                    R9[47Ω] → GND
Acl = 1 + Rfb / R9 = 1 + Rfb / 47
```

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S3-1 | Creer `CEStageWDF.h` — etage emetteur commun generique | `core/include/core/preamp/CEStageWDF.h` | NEW | L | P0 |
| S3-2 | Creer `EFStageWDF.h` — etage emetteur-suiveur | `core/include/core/preamp/EFStageWDF.h` | NEW | M | P0 |
| S3-3 | Creer `NeveClassAPath.h` — assemblage 3 etages + feedback | `core/include/core/preamp/NeveClassAPath.h` | NEW | XL | P0 |
| S3-4 | Implementer le feedback explicite (output → input via Rfb) | `NeveClassAPath.h` | INCL | L | P0 |
| S3-5 | Operating point solver (DC bias computation at init) | `NeveClassAPath.h` | INCL | L | P1 |
| S3-6 | Test: Neve path gain vs position (11 valeurs) | `Tests/test_neve_path.cpp` | NEW | M | P0 |
| S3-7 | Test: Neve path freq response (1 Hz - 80 kHz target) | `Tests/test_neve_path.cpp` | EXTEND | L | P0 |
| S3-8 | Test: Neve path THD vs level (harmoniques paires dominantes) | `Tests/test_neve_path.cpp` | EXTEND | L | P1 |
| S3-9 | Test: Neve path soft clipping (compression naturelle) | `Tests/test_neve_path.cpp` | EXTEND | M | P1 |
| S3-10 | Register test in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### CEStageWDF Design

```cpp
// Generic common-emitter WDF stage (reusable for Q1 and Q2)
template <typename NonlinearLeaf = BJTLeaf>
class CEStageWDF {
public:
    struct Config {
        BJTParams bjt;
        float R_collector;      // Charge collecteur [Ω]
        float R_emitter_dc;     // RE DC bias [Ω]
        float R_emitter_ac;     // RE AC (en parallele si C bypass) [Ω]
        float R_base_bias_top;  // Bias top [Ω]
        float R_base_bias_bot;  // Bias bottom [Ω]
        float C_input;          // Couplage entree [F] (0 = DC-coupled)
        float C_miller;         // Compensation [F]
        float C_emitter_bypass; // Bypass emitter [F] (0 = none)
        float Vcc;              // Supply positive [V]
        float Vee;              // Supply negative [V]
    };

    void prepare(float sampleRate, const Config& config);
    void reset();
    float processSample(float input);

    // Monitoring
    float getVce() const;
    float getIc() const;
    float getGainInstantaneous() const;

private:
    NonlinearLeaf bjtLeaf_;
    AdaptedCapacitor cInput_, cMiller_, cBypass_;
    AdaptedResistor rCollector_, rEmitter_, rBaseBiasTop_, rBaseBiasBot_;
    WDFSeriesAdaptor sInput_;
    DynamicParallelAdaptor<3> pCollector_;
    // ... WDF tree construction
};
```

### NeveClassAPath Design

```cpp
class NeveClassAPath : public IAmplifierPath {
public:
    void prepare(float sampleRate, int maxBlockSize) override;
    void reset() override;
    float processSample(float input) override;
    void processBlock(const float* in, float* out, int n) override;
    void setGain(float Rfb) override;
    float getOutputImpedance() const override;  // ~11Ω
    const char* getName() const override { return "Neve Heritage"; }

    void configure(const NevePathConfig& config);

private:
    CEStageWDF<BJTLeaf> q1Stage_;    // BC184C CE
    CEStageWDF<BJTLeaf> q2Stage_;    // BC214C CE (PNP)
    EFStageWDF<BJTLeaf> q3Stage_;    // BD139 EF

    // Feedback path
    AdaptedCapacitor cFeedback_;     // C6 = 470µF
    float Rfb_ = 1430.0f;           // Current gain position
    float Rg_ = 47.0f;              // Reference

    // Coupling output
    AdaptedCapacitor cOutput_;       // 220µF + 4.7µF
    AdaptedResistor rSeriesOut_;     // 10Ω

    // DC operating point
    struct OperatingPoint {
        float Vce_q1, Ic_q1;
        float Vce_q2, Ic_q2;
        float Vce_q3, Ic_q3;
    } opPoint_;

    void solveOperatingPoint();      // DC solution at init
    float applyFeedback(float output, float& feedbackState);
};
```

### Acceptance Criteria

- [ ] Gain position 1 → gain mesure ≈ +10 dB (±1 dB)
- [ ] Gain position 6 → gain mesure ≈ +30 dB (±1 dB)
- [ ] Gain position 11 → gain mesure ≈ +50 dB (±1 dB)
- [ ] Bande passante: -3dB @ ~1 Hz (bas) et ~80 kHz (haut) au gain +30dB
- [ ] THD @+4dBu, 1kHz < 0.1% (ampli seul, sans transfo)
- [ ] Harmoniques paires dominantes (H2 > H3) — signature Classe-A
- [ ] Soft clipping: compression progressive >+20 dBu (pas de hard clip)
- [ ] Zout ≈ 11Ω (1/gm_Q3 + R_series)
- [ ] Operating point stable: Ic_Q3 ≈ 33 mA, Vce_Q3 ≈ 35V
- [ ] Pas de NaN/Inf, pas d'oscillation
- [ ] Tests existants passent

---

## Sprint 4 — Chemin B: JE-990 DIY

**Goal**: Implementer l'op-amp discret JE-990 (8 transistors) avec paire differentielle
LM-394, cascode PNP, VAS Miller, sortie Class-AB et Load Isolator.

**Sprint Duration**: 3 semaines (sprint le plus complexe)

### Decomposition en sous-circuits

Le JE-990 est decompose en **4 etages WDF** cascades :

```
Etage 1: Paire differentielle          Etage 2: Cascode + Miroir
┌─────────────────────────────┐        ┌──────────────────────────┐
│ (+) → CR1 → Q1 (LM-394 #1) │        │ Q3 (2N4250A) PNP cascode │
│              |E              │   →    │ Q5 (2N4250A) PNP cascode │
│         R1[30] + L1[20µH]   │        │ R4,R5[300] charges       │
│              |               │        │ CR3 bias diode           │
│         Q4 (2N2484) tail    │        │ Conversion diff→SE       │
│              |               │        └────────────┬─────────────┘
│         R3[160]             │                      |
│              |               │                      ↓
│ (-) → CR2 → Q2 (LM-394 #2) │        Etage 3: VAS (Voltage Amp Stage)
│              |E              │        ┌──────────────────────────┐
│         R2[30] + L2[20µH]   │        │ Q6 (2N4250A) PNP VAS     │
└─────────────────────────────┘        │ C1[150pF] Miller comp    │
                                        │ R7[160], R8[130]         │
                                        └────────────┬─────────────┘
                                                     |
                                                     ↓
                                        Etage 4: Sortie Class-AB
                                        ┌──────────────────────────┐
                                        │ Q8 (MJE-181) NPN top     │
                                        │ Q7 (2N2484) pre-driver   │
                                        │ MJE-171 PNP bottom       │
                                        │ C2[62p], C3[91p] comp    │
                                        │ R13[3.9], R14,R15[39]    │
                                        │ L3[40µH] output isolator │
                                        │ CR5-CR12 protection      │
                                        └──────────────────────────┘
```

### Simplifications pragmatiques

Pour une implementation realiste (vs SPICE full-circuit) :

1. **Etage 2 (Cascode)** : Linearise — le cascode a un point de fonctionnement
   quasi-fixe. Modele comme un gain + impedance de sortie, pas de NL element.
2. **Diodes de protection** (CR1-CR12) : Modeles uniquement quand le signal
   depasse le seuil (~0.6V). Bypass en fonctionnement normal.
3. **Source de courant Q4** : Modele comme courant constant (I_tail ≈ 3mA),
   pas besoin de WDF full pour un element quasi-ideal.
4. **Inductances L1, L2** : `AdaptedInductor` (elements lineaires existants).

**Elements non-lineaires effectifs** : Q1/Q2 (diff pair), Q6 (VAS), Q8+Q9 (output)
= **4 NL elements** dans 3 sous-circuits cascades.

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S4-1 | Creer `DiffPairWDF.h` — paire differentielle WDF | `core/include/core/preamp/DiffPairWDF.h` | NEW | XL | P0 |
| S4-2 | Creer `CascodeStage.h` — etage cascode linearise | `core/include/core/preamp/CascodeStage.h` | NEW | M | P0 |
| S4-3 | Creer `VASStageWDF.h` — VAS avec Miller compensation | `core/include/core/preamp/VASStageWDF.h` | NEW | L | P0 |
| S4-4 | Creer `ClassABOutputWDF.h` — sortie push-pull | `core/include/core/preamp/ClassABOutputWDF.h` | NEW | XL | P0 |
| S4-5 | Creer `LoadIsolator.h` — 39Ω + L3=40µH | `core/include/core/preamp/LoadIsolator.h` | NEW | S | P1 |
| S4-6 | Creer `JE990Path.h` — assemblage complet + feedback | `core/include/core/preamp/JE990Path.h` | NEW | XL | P0 |
| S4-7 | Test: JE-990 gain vs position | `Tests/test_je990_path.cpp` | NEW | M | P0 |
| S4-8 | Test: JE-990 freq response (0.5 Hz - 200 kHz target) | `Tests/test_je990_path.cpp` | EXTEND | L | P0 |
| S4-9 | Test: JE-990 THD (< 0.002% @+4dBu) | `Tests/test_je990_path.cpp` | EXTEND | L | P1 |
| S4-10 | Test: JE-990 inductances L1/L2 effect on THD HF | `Tests/test_je990_path.cpp` | EXTEND | M | P1 |
| S4-11 | Test: Load isolator attenuation HF | `Tests/test_je990_path.cpp` | EXTEND | M | P1 |
| S4-12 | Register test in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### DiffPairWDF Design

```cpp
// Differential pair with emitter degeneration + inductances
class DiffPairWDF {
public:
    struct Config {
        BJTParams q1q2;         // LM-394 matched pair
        float R_emitter = 30.0f;  // R1, R2 degeneration [Ω]
        float L_emitter = 20e-6f; // L1, L2 Jensen inductances [H]
        float I_tail = 3e-3f;    // Tail current source [A]
        float R_tail = 160.0f;   // R3, Q4 → simplified
    };

    void prepare(float sampleRate, const Config& config);
    void reset();

    // Differential input, single-ended output
    float processSample(float vPlus, float vMinus);

    // Monitoring
    float getVdiff() const;     // V+ - V-
    float getGm() const;        // Effective diff pair gm
    float getCMRR() const;      // Common-mode rejection

private:
    BJTLeaf q1_, q2_;
    AdaptedResistor rE1_, rE2_;
    AdaptedInductor lE1_, lE2_;     // Jensen L1, L2
    // Tail current as ideal source
    float I_tail_;
};
```

### JE990Path Design

```cpp
class JE990Path : public IAmplifierPath {
public:
    void prepare(float sampleRate, int maxBlockSize) override;
    void reset() override;
    float processSample(float input) override;
    void processBlock(const float* in, float* out, int n) override;
    void setGain(float Rfb) override;
    float getOutputImpedance() const override;  // < 5Ω
    const char* getName() const override { return "Jensen Heritage"; }

    void configure(const JE990PathConfig& config);

private:
    DiffPairWDF diffPair_;          // Etage 1: LM-394
    CascodeStage cascode_;          // Etage 2: Q3, Q5 (linearise)
    VASStageWDF vas_;               // Etage 3: Q6 + C1=150pF
    ClassABOutputWDF outputStage_;  // Etage 4: Q8/MJE-171
    LoadIsolator loadIsolator_;     // 39Ω + L3=40µH

    // Feedback
    float Rfb_ = 1430.0f;
    float Rg_ = 47.0f;

    // Coupling output
    AdaptedCapacitor cOutput_;      // 220µF + 4.7µF

    // Feedback is from output back to (-) input of diff pair
    float feedbackState_ = 0.0f;
};
```

### Acceptance Criteria

- [ ] Gain position 1 → +10 dB (±1 dB)
- [ ] Gain position 11 → +50 dB (±1 dB)
- [ ] Bande passante: -3dB @ ~0.5 Hz (bas) et ~200 kHz (haut)
- [ ] THD @+4dBu, 1kHz < 0.01% (ampli seul)
- [ ] THD HF: avec L1/L2 < sans L1/L2 (verification de la linearisation HF)
- [ ] Harmoniques impaires (H3, H5) dominantes — signature Class-AB push-pull
- [ ] Zout < 5Ω avant load isolator
- [ ] Load isolator : attenuation >-3dB au-dessus de 100kHz
- [ ] Courant de sortie peak > 100mA (test avec charge 100Ω)
- [ ] Pas de NaN/Inf, pas d'oscillation (compensation C1+C2+C3 suffisante)
- [ ] Tests existants passent

---

## Sprint 5 — Output Stage & A/B Switching

**Goal**: Circuit de sortie partage : couplage AC, commutation A/B par crossfade,
et integration du JT-11ELCF (T2). Le signal sort en XLR balanced.

**Sprint Duration**: 1.5 semaines

### Schema

```
         Chemin A output              Chemin B output
              |                            |
         C_outA [220µF+4.7µF]        C_outB [220µF+4.7µF]
              |                            |
         R_serA [10Ω]                 R_loadB [39Ω] + L3[40µH]
              |                            |
              └──────────┐  ┌──────────────┘
                         |  |
                    [ ABCrossfade ]
                    cos²/sin² morph
                         |
                    JT-11ELCF (T2)
                    TransformerCircuitWDF
                         |
                    XLR OUT (balanced)
```

### A/B Crossfade (sans click)

Plutot qu'un relay dur (discontinuite), on utilise un **crossfade
cos²/sin²** de 3-5ms pour une transition sans artifact :

```cpp
class ABCrossfade {
    float position_ = 0.0f;    // 0 = Chemin A, 1 = Chemin B
    float target_ = 0.0f;
    float smoothCoeff_;         // exp decay, τ = 3ms

    float process(float sampleA, float sampleB) {
        position_ += smoothCoeff_ * (target_ - position_);
        const float gA = std::cos(position_ * kHalfPi);
        const float gB = std::sin(position_ * kHalfPi);
        return gA * sampleA + gB * sampleB;
    }
};
```

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S5-1 | Creer `OutputStageWDF.h` — couplage AC + T2 | `core/include/core/preamp/OutputStageWDF.h` | NEW | M | P0 |
| S5-2 | Creer `ABCrossfade.h` — crossfade cos²/sin² | `core/include/core/preamp/ABCrossfade.h` | NEW | S | P0 |
| S5-3 | Integrer T2 via TransformerCircuitWDF existant | `OutputStageWDF.h` | INCL | M | P0 |
| S5-4 | Modeliser Zout_path → impact sur T2 (impedance source) | `OutputStageWDF.h` | INCL | M | P1 |
| S5-5 | Test: A/B crossfade sans clicks (zero-crossing analysis) | `Tests/test_output_stage.cpp` | NEW | M | P0 |
| S5-6 | Test: T2 insertion loss (~-1.1 dB) | `Tests/test_output_stage.cpp` | EXTEND | M | P0 |
| S5-7 | Test: Zout differentielle ≈ 80-91Ω (T2 dominant) | `Tests/test_output_stage.cpp` | EXTEND | S | P1 |
| S5-8 | Register test in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### OutputStageWDF Design

```cpp
class OutputStageWDF {
public:
    void prepare(float sampleRate, const TransformerConfig& t2Config);
    void reset();

    // Process both paths, output selected/crossfaded
    float processSample(float sampleA, float sampleB,
                        float Zout_A, float Zout_B);

    // A/B control
    void setPath(float position);  // 0=A, 1=B (smooth transition)
    bool isTransitioning() const;

    // Monitoring
    float getT2MagnetizingCurrent() const;
    float getOutputLevel() const;

private:
    ABCrossfade crossfade_;
    TransformerCircuitWDF<...> t2_;  // JT-11ELCF model

    // The source impedance seen by T2 changes with path selection
    void updateT2SourceImpedance(float Zout_active);
};
```

### Acceptance Criteria

- [ ] Crossfade A→B : pas de click audible (discontinuite < -60dB)
- [ ] Crossfade duree ≈ 3-5ms (smooth, pas de fade long)
- [ ] T2 insertion loss = -1.1 dB ±0.2 dB (Rs < 11Ω, Chemin A)
- [ ] T2 insertion loss ≈ -1.1 dB (Rs < 5Ω, Chemin B)
- [ ] Zout differentielle mesuree ≈ 80-91Ω
- [ ] T2 coloration LF visible a +20 dBu @20Hz (saturation douce)
- [ ] T2 hors boucle feedback → distorsion non-corrigee (spec §2.7)
- [ ] Tests existants passent

---

## Sprint 6 — PreampModel: Full Chain Integration & Gain Control

**Goal**: Assembler le PreampModel complet : InputStage → [A|B] → OutputStage.
Ajouter le controle de gain 11 positions, mode Physical/Realtime, et
l'orchestration du pipeline.

**Sprint Duration**: 2 semaines

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S6-1 | Creer `PreampModel.h` — orchestrateur top-level | `core/include/core/preamp/PreampModel.h` | NEW | XL | P0 |
| S6-2 | Pipeline: Input → Amp → Output avec gain smoothing | `PreampModel.h` | INCL | L | P0 |
| S6-3 | Gain control: 11 positions, smooth transitions | `PreampModel.h` | INCL | M | P0 |
| S6-4 | Mode Physical (J-A + oversampling) vs Realtime (CPWL) | `PreampModel.h` | INCL | L | P0 |
| S6-5 | Controls runtime: pad, ratio, path, gain, phase | `PreampModel.h` | INCL | M | P1 |
| S6-6 | Test: full chain gain positions (T1+amp+T2) | `Tests/test_preamp_full.cpp` | NEW | L | P0 |
| S6-7 | Test: full chain freq response A vs B | `Tests/test_preamp_full.cpp` | EXTEND | L | P0 |
| S6-8 | Test: pad ON/OFF impact sur coloration LF | `Tests/test_preamp_full.cpp` | EXTEND | M | P1 |
| S6-9 | Test: ratio 1:5 vs 1:10 gain difference ≈ 6dB | `Tests/test_preamp_full.cpp` | EXTEND | M | P1 |
| S6-10 | Register test in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### PreampModel Design

```cpp
template <typename NonlinearLeaf>
class PreampModel {
public:
    // Lifecycle
    void setConfig(const PreampConfig& config);
    void prepareToPlay(float sampleRate, int maxBlockSize);
    void reset();

    // Processing
    void processBlock(const float* input, float* output, int numSamples);

    // Controls (thread-safe, smoothed)
    void setGainPosition(int position);     // 0-10 (11 steps)
    void setPath(int path);                 // 0=Neve, 1=Jensen
    void setPadEnabled(bool enabled);
    void setRatio(int ratio);               // 0=1:5, 1=1:10
    void setPhaseInvert(bool invert);
    void setInputGain(float dB);
    void setOutputGain(float dB);
    void setMix(float wetDry);              // 0=dry, 1=wet

    // Monitoring
    struct MonitorData {
        float inputLevel_dBu;
        float outputLevel_dBu;
        float t1_magnetizing_current;
        float t2_magnetizing_current;
        float amp_Vce[3];          // Operating points (Neve)
        float amp_Ic[3];
        int currentPath;           // 0=A, 1=B
        int gainPosition;
        bool isClipping;
    };
    MonitorData getMonitorData() const;

private:
    // Pipeline stages
    InputStageWDF inputStage_;

    // Strategy paths (both always prepared, only active one processes)
    NeveClassAPath nevePathA_;
    JE990Path je990PathB_;

    OutputStageWDF outputStage_;

    // Gain
    GainTable gainTable_;
    int gainPosition_ = 5;          // Default +30dB
    SmoothedValue<float> gainSmoothed_;

    // Controls
    std::atomic<int> targetPath_{0};
    std::atomic<bool> padEnabled_{false};
    std::atomic<int> ratio_{1};     // 0=1:5, 1=1:10
    std::atomic<bool> phaseInvert_{false};

    SmoothedValue<float> inputGain_, outputGain_, mix_;

    // Processing mode
    enum class Mode { Physical, Realtime } mode_ = Mode::Realtime;
    OversamplingEngine oversampler_;  // Physical mode only

    // Internal processing
    void processBlockInternal(const float* in, float* out, int n);
    float processSampleInternal(float sample);
};
```

### Niveaux attendus (de ANALYSE_ET_DESIGN_Rev2.md §2.6)

```
                        Chemin A (Neve)              Chemin B (JE-990)
                        ═══════════════              ═════════════════
Gain total (1:10):      +24 to +70 dB                +23 to +69 dB
Gain total (1:5):       +18 to +64 dB                +17 to +63 dB

SM57 voix normale (-40 dBu), gain +30dB, 1:10:
  Sec T1:  -20 dBu
  Sort amp: +10 dBu
  Sort T2:  +9 dBu → OK

U87 forte SPL (-10 dBu), pad ON, gain +30dB, 1:10:
  Sec T1:  -10 dBu (pad attenuation)
  Sort amp: +20 dBu
  Sort T2:  +19 dBu → OK
```

### Acceptance Criteria

- [ ] Full chain: SM57 -40dBu, gain pos 6 (+30dB), 1:10 → sortie ≈ +9 dBu
- [ ] Full chain: gain pos 1 à 11 → niveaux coherents avec le tableau §2.6
- [ ] Chemin A: bande 1 Hz - 80 kHz (-3dB)
- [ ] Chemin B: bande 0.5 Hz - 200 kHz (-3dB)
- [ ] A/B switch: meme gain ±1dB a position identique
- [ ] Pad ON/OFF: attenuation ≈ 15-20 dB
- [ ] Ratio 1:5 vs 1:10: difference ≈ 6 dB
- [ ] Phase invert: inversion exacte (correlation = -1.0)
- [ ] Mode Physical vs Realtime: difference < 1dB sur tout le spectre
- [ ] Oversampling 4x fonctionne en mode Physical
- [ ] Tests existants passent

---

## Sprint 7 — Plugin Integration & UI

**Goal**: Exposer tous les parametres du preamp dans le plugin JUCE. Mettre a jour
l'editeur avec les controles face avant. Serialisation des presets.

**Sprint Duration**: 2 semaines

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S7-1 | Ajouter PreampModel dans PluginProcessor | `plugin/Source/PluginProcessor.h/.cpp` | MODIFY | L | P0 |
| S7-2 | Parametres APVTS: gain, path, pad, ratio, phase | `plugin/Source/ParameterLayout.h` | MODIFY | M | P0 |
| S7-3 | UI: bouton gain rotatif 11 positions | `plugin/Source/PluginEditor.h/.cpp` | MODIFY | M | P1 |
| S7-4 | UI: switch A/B avec LEDs indicatrices | `plugin/Source/PluginEditor.h/.cpp` | MODIFY | M | P1 |
| S7-5 | UI: toggles 48V, PAD, HPF, LINE, PHASE | `plugin/Source/PluginEditor.h/.cpp` | MODIFY | M | P1 |
| S7-6 | Serialisation preset (save/load state) | `plugin/Source/PluginProcessor.cpp` | MODIFY | M | P0 |
| S7-7 | Test: plugin integration (instanciation, process) | `Tests/test_preamp_plugin.cpp` | NEW | M | P0 |
| S7-8 | Register test in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### Face avant (de PREAMP_OVERVIEW.md)

```
+-----------------------------------------------------------------------+
|                                                                       |
|  [48V]  [PAD]  [HPF]  [LINE]     [ GAIN ]     [A/B]  [PHASE]  (o)(o)|
|  toggle toggle toggle toggle   11 positions   NEVE   toggle   LED LED|
|                                  Grayhill      /990            A   B  |
|                                                                       |
+-----------------------------------------------------------------------+

APVTS Parameters:
  - "phantom"   : bool
  - "pad"       : bool
  - "hpf"       : bool (futur — filtre passe-haut)
  - "line"      : bool (futur — entree ligne directe)
  - "gain"      : int [0..10] (11 positions)
  - "path"      : int [0..1] (0=Neve, 1=Jensen)
  - "phase"     : bool (inversion)
  - "ratio"     : int [0..1] (0=1:5, 1=1:10)
  - "inputGain" : float [-12..+12] dB
  - "outputGain": float [-12..+12] dB
  - "mix"       : float [0..1]
```

### Acceptance Criteria

- [ ] Plugin compile et s'instancie sans crash
- [ ] Tous les parametres controlables via APVTS
- [ ] Save/Load de l'etat complet (preset persistence)
- [ ] UI responsive (pas de blocage audio thread)
- [ ] Changement de path A/B: crossfade audible, pas de click
- [ ] Changement de gain: transition smooth (pas de pop)
- [ ] Tests existants passent + nouveau test plugin integration

---

## Sprint 8 — Validation, Optimisation & Polish

**Goal**: Validation complete des specifications, optimisation CPU, regression,
et tests d'ecoute A/B.

**Sprint Duration**: 2 semaines

### Tickets

| ID | Task | File | Type | Est | Priorite |
|----|------|------|------|-----|----------|
| S8-1 | Validation: specifications Chemin A (tableau §3.2) | `Tests/test_preamp_validation.cpp` | NEW | L | P0 |
| S8-2 | Validation: specifications Chemin B (tableau §3.3) | `Tests/test_preamp_validation.cpp` | EXTEND | L | P0 |
| S8-3 | Validation: niveaux noeud-par-noeud (tableau §2.6) | `Tests/test_preamp_validation.cpp` | EXTEND | L | P0 |
| S8-4 | Validation: interaction pad/ratio/gain (tous combos) | `Tests/test_preamp_validation.cpp` | EXTEND | XL | P1 |
| S8-5 | CPU profiling: budget < 30% single core @44.1kHz | Manual | TEST | M | P0 |
| S8-6 | Optimisation: hot loops, SIMD, adaptation interval | All preamp/ | MODIFY | L | P1 |
| S8-7 | Full regression: tous tests (anciens + nouveaux) | All tests | TEST | M | P0 |
| S8-8 | Test d'ecoute A/B: process musique via les 2 chemins | Manual | TEST | L | P1 |
| S8-9 | Cleanup: dead code, feature flags, documentation | All | MODIFY | M | P2 |
| S8-10 | Update root CMakeLists.txt (tous les headers) | `CMakeLists.txt` | MODIFY | S | P0 |
| S8-11 | Register validation test in CMake | `Tests/CMakeLists.txt` | MODIFY | S | P0 |

### Verification Matrix

| Test | Sprint | Valide |
|------|--------|--------|
| test_diode_leaf | S1 | DiodeLeaf I-V, convergence NR |
| test_bjt_leaf | S1 | BJTLeaf Ic-Vce, NPN/PNP, passivite |
| test_input_stage | S2 | T1 + terminaison, Zs_eff, pad impact |
| test_neve_path | S3 | Gain, BW, THD, soft clip, Class-A |
| test_je990_path | S4 | Gain, BW, THD, L1/L2 effect, load isolator |
| test_output_stage | S5 | Crossfade, T2 insertion loss, Zout |
| test_preamp_full | S6 | Full chain, all controls, A/B compare |
| test_preamp_plugin | S7 | Plugin instanciation, APVTS, save/load |
| test_preamp_validation | S8 | Specs Rev 2.0 compliance |
| *tests existants* | S8 | Zero regression (cpwl, passivity, hsim, etc.) |

### Specifications cibles (de ANALYSE_ET_DESIGN_Rev2.md)

```
                        Chemin A (Neve)              Chemin B (JE-990)
                        ═══════════════              ═════════════════
EIN (150Ω, A-wtd)      < -126 dBu                   < -130 dBu
THD+N @+4dBu, 1kHz     < 0.02%                      < 0.002%
Sortie max (600Ω)      +24 dBu                      +24 dBu
Courant sortie peak    ~33 mA                        > 100 mA
Zout avant T2          ~11 Ω                         < 5 Ω
Bande passante         1 Hz - 80 kHz                 0.5 Hz - 200 kHz
Caractere              Chaud, gras, compresse         Precis, musical, ouvert
```

### CPU Budget

```
Component                    Ops/sample    % of total
──────────────────────────────────────────────────────
T1 WDF tree (existing)       ~172          20%
  - HSIM solver (3 NL)       ~80
  - LC resonance             ~30
  - Oversampling filter      ~40
  - Adaptor recalc           ~22
Amplifier Stage (new)        ~300-500      40-55%
  - Chemin A (3 BJT NR)      ~150
  - OR Chemin B (4 NL + lin) ~350
  - Feedback computation     ~20
  - Gain smoothing           ~5
T2 WDF tree (existing)       ~172          20%
Output processing            ~30           5%
  - Crossfade                ~5
  - Mix, gain                ~10
  - Coupling caps            ~15
──────────────────────────────────────────────────────
Total per sample (Realtime)  ~500-700
At 44.1 kHz                  ~22-31M ops/s/channel

Physical mode (4x OS):       ~2000-2800 ops/OS sample
At 176.4 kHz                 ~350-500M ops/s/channel
→ ~15-20% single core (modern CPU)
```

### Profil de distorsion attendu (de ANALYSE_ET_DESIGN_Rev2.md §2.4)

```
┌──────────────┬──────────────────────────────────────────────────┐
│ Bande        │ Mecanisme dominant                               │
├──────────────┼──────────────────────────────────────────────────┤
│ 20-100 Hz    │ Saturation noyau T1+T2 (J-A hysteresis)          │
│              │ → H3, H5 dominants, compression douce            │
│              │ → PLUS prononce avec pad ON (Zs elevee, §2.3)    │
├──────────────┼──────────────────────────────────────────────────┤
│ 100-2 kHz    │ Faible flux, transfos quasi lineaires             │
│              │ → Chemin A: harmoniques paires (Classe-A)         │
│              │ → Chemin B: quasi transparent (JE-990)            │
├──────────────┼──────────────────────────────────────────────────┤
│ 2-20 kHz     │ Inductance fuite + capacites parasites           │
│              │ → Resonances LC, rolloff, terminaison 13.7K+680pF│
│              │ → Chemin B: L1/L2 linearisent les HF du 990      │
├──────────────┼──────────────────────────────────────────────────┤
│ > 20 kHz     │ T2 hors boucle: coloration libre                 │
│              │ → "Vernis final" du son (Neve, API = meme concept)│
└──────────────┴──────────────────────────────────────────────────┘
```

### Risk Register

| Risk | Impact | Probabilite | Mitigation |
|------|--------|-------------|------------|
| BJT Newton-Raphson divergence | Audio artifacts | Moyenne | Warm-start, clamping Vbe, ConvergenceGuard |
| Feedback loop instability | Oscillation | **CONFIRME** | **Gain analytique Acl=1+Rfb/Rg** (voir LESSONS_WDF_PREAMP.md §2) |
| JE-990 CPU trop eleve (8 BJT) | Performance | Moyenne | Cascode linearise, adaptation interval >16 samples |
| Couplage inter-etages en WDF | Imprecision | **CONFIRME** | **Couplage AC + bias independante** (voir LESSONS_WDF_PREAMP.md §7) |
| LM-394 matching numerique | Drift diff pair | Faible | Double precision pour paire diff |
| T2 interaction avec Zout variable | Coloration inattendue | Faible | Zout update vers T2 a chaque switch A/B |
| Classe-A operating point shift | Distorsion erronee | **CONFIRME** | **V_bias auto-calcule + warmup 32 samples** (voir LESSONS_WDF_PREAMP.md §1,4) |
| Click au changement de gain | Audio artifacts | Moyenne | SmoothedValue sur Rfb, rampe 10ms |
| Transitoire post-reset | Rail-to-rail ±24V | **CONFIRME** | **Warmup + init filtres a Vc_quiescent** (voir LESSONS_WDF_PREAMP.md §4,5) |

> **Note** : Les risques marques **CONFIRME** ont ete rencontres et resolus
> pendant le Sprint 3. Voir `docs/LESSONS_WDF_PREAMP.md` pour les details
> complets et les regles a appliquer aux sprints suivants.

---

## Summary

| Sprint | Focus | New Files | Modified Files | New Tests | Duration |
|--------|-------|-----------|----------------|-----------|----------|
| **S0** | Foundation: configs, interfaces | 4 | 1 | 0 | 1 sem |
| **S1** | BJT & Diode WDF elements | 3 | 1 | 2 | 2 sem |
| **S2** | Input Stage WDF | 1 | 1 | 1 | 1.5 sem |
| **S3** | Chemin A: Neve Class-A | 3 | 1 | 1 | 2.5 sem |
| **S4** | Chemin B: JE-990 DIY | 6 | 1 | 1 | 3 sem |
| **S5** | Output Stage & A/B | 2 | 1 | 1 | 1.5 sem |
| **S6** | PreampModel full chain | 1 | 1 | 1 | 2 sem |
| **S7** | Plugin integration & UI | 0 | 4 | 1 | 2 sem |
| **S8** | Validation & polish | 0 | 3 | 1 | 2 sem |

**Total**: ~20 new files, ~14 modified files, 9 new test files
**Duration estimee**: ~17.5 semaines (~4.5 mois)

### Critical Path

```
S0 (configs) ──→ S1 (BJT/Diode WDF) ──→ S3 (Chemin A: Neve)
                      │                       │
                      └──→ S2 (Input Stage) ──┤
                                              │
                      S1 ──→ S4 (Chemin B: JE-990)
                                              │
                                              ├──→ S5 (Output Stage)
                                              │
                                              └──→ S6 (PreampModel)
                                                        │
                                                   S7 (Plugin)
                                                        │
                                                   S8 (Validation)
```

**Parallelisme possible** : S3 (Neve) et S4 (JE-990) peuvent etre developpes
en parallele apres S1, a condition que S2 (Input) soit termine ou mocke.

### File Tree (nouveaux fichiers)

```
core/include/core/
├── preamp/                              ← NOUVEAU DOSSIER
│   ├── IAmplifierPath.h                 [S0] Interface Strategy
│   ├── PreampConfig.h                   [S0] Config complete
│   ├── BJTParams.h                      [S0] Parametres Ebers-Moll
│   ├── GainTable.h                      [S0] 11 positions Rfb
│   ├── InputStageWDF.h                  [S2] Circuit d'entree
│   ├── CEStageWDF.h                     [S3] Etage emetteur commun
│   ├── EFStageWDF.h                     [S3] Etage emetteur-suiveur
│   ├── NeveClassAPath.h                 [S3] Strategy A
│   ├── DiffPairWDF.h                    [S4] Paire differentielle
│   ├── CascodeStage.h                   [S4] Cascode linearise
│   ├── VASStageWDF.h                    [S4] VAS + Miller
│   ├── ClassABOutputWDF.h               [S4] Sortie push-pull
│   ├── LoadIsolator.h                   [S4] 39Ω + 40µH
│   ├── JE990Path.h                      [S4] Strategy B
│   ├── OutputStageWDF.h                 [S5] Sortie + T2
│   ├── ABCrossfade.h                    [S5] Crossfade A/B
│   └── PreampModel.h                    [S6] Orchestrateur
│
├── wdf/
│   ├── DiodeLeaf.h                      [S1] Diode WDF non-lineaire
│   ├── BJTLeaf.h                        [S1] BJT WDF Ebers-Moll
│   └── BJTCompanionModel.h              [S1] Companion linearisation
│
Tests/
├── test_diode_leaf.cpp                  [S1]
├── test_bjt_leaf.cpp                    [S1]
├── test_input_stage.cpp                 [S2]
├── test_neve_path.cpp                   [S3]
├── test_je990_path.cpp                  [S4]
├── test_output_stage.cpp                [S5]
├── test_preamp_full.cpp                 [S6]
├── test_preamp_plugin.cpp               [S7]
└── test_preamp_validation.cpp           [S8]
```

---

*Sprint Plan — Dual-Topology Discrete Preamp Model*
*Base sur ANALYSE_ET_DESIGN_Rev2.md (Rev 2.0) et PREAMP_OVERVIEW.md*
*Architecture : WDF-hybrid, Strategy pattern, CRTP, Pipeline*
*Circuits de reference : Neve 1099/BA283, SSL 82E01, JE-990 (Deane Jensen 1980)*
