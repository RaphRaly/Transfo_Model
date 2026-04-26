# Rapport Phase 1 + Phase 2 — JE990 WDF Double Precision & VAS Topology Fix

**Date:** 2026-03-26
**Auteur:** Claude Opus 4.6 (session de travail avec l'utilisateur)
**Contexte:** Suite de l'analyse KVL/KCL documentée dans `ANALYSIS_JE990_KVL_KCL_2026-03-26.md`
**Destinataire:** GPT-5.4 pour continuation du debug

---

## 1. Résumé des modifications effectuées

Deux phases implémentées dans cette session :
- **Phase 1** — Double precision dans le chemin critique du diff pair (cancellation float32)
- **Phase 2** — Correction topologique du VAS (R7/R8 inversés) + réécriture du JE990Path

Le Newton solver original (backbone×500 + softSat + commit beta*yk) a été **jeté et réécrit de zéro** car il ne fonctionnait pas (locking aux rails ±23V).

---

## 2. Fichiers modifiés — Phase 1 (Double Precision)

### 2.1 `core/include/core/wdf/BJTCompanionModel.h`

**Problème :** Le NR solver interne travaille en double, mais cast les résultats en float avant stockage. Perte de précision sur Vbe, Ic, Ib, gm.

**Modifications :**
- **Membres privés** `float → double` :
  ```
  sign_      : float → double
  Vbe_prev_  : float → double
  Ic_last_   : float → double
  Ib_last_   : float → double
  gm_        : float → double
  ```
- **`solve()`** : retourne `double` au lieu de `float`. Suppression des `static_cast<float>(Vbe)`, `static_cast<float>(sign_ * Is / Bf * ...)` etc. Les résultats NR restent en double bout-en-bout.
- **Tous les getters** retournent `double` :
  - `getCollectorCurrent()`, `getBaseCurrent()`, `getTransconductance()`, `getVbe()`
  - `getCompanionResistance()`, `getOutputResistance()`
- **`restoreState()`** : paramètres `float → double`
- **`reset()`** : littéraux `0.6f → 0.6`, `0.0f → 0.0`
- **`configure()`** : `sign_ = static_cast<double>(params.polaritySign())`

### 2.2 `core/include/core/wdf/BJTLeaf.h`

**Problème :** BJTLeaf expose les valeurs du companion en float, perdant la précision double du NR.

**Modifications :**
- **`scatterImpl()`** : `Vbe` stocké en `double` depuis `companion_.solve()`. Le scatter wave reste float (interface WDOnePort) : `return static_cast<float>(2.0 * Vbe - static_cast<double>(a))`
- **Getters** retournent `double` :
  - `getCollectorCurrent()`, `getVbe()`, `getIc()`, `getIb()`, `getGm()`
  - `getSmallSignalRbe()`, `getSmallSignalRce()`
- **`AcState` struct** : `Vbe_prev, Ic_last, Ib_last, gm` passés de `float` à `double`

**Note :** Les wave variables `a_incident_`, `b_reflected_` restent `float` (dans `WDOnePort` base class). Pas de cancellation sur ces valeurs (elles sont des tensions absolues, pas des différences de µV).

### 2.3 `core/include/core/preamp/DiffPairWDF.h`

**Problème central :** Catastrophic cancellation IEEE 754 float32 quand V_bias (~0.63V) + signal µV sont additionnés. La différence Ic1 - Ic2 (deux courants ~1.5mA) perd aussi ses bits significatifs en float.

**Modifications :**
- **`processSample()` — baseDrive en double** (LE fix critique) :
  ```cpp
  // AVANT (float32 — signal perdu) :
  const float baseDrive1 = vPlus + V_bias_base_;
  // APRÈS (double — signal préservé) :
  const double baseDrive1 = static_cast<double>(vPlus) + V_bias_base_;
  ```
- **Incident wave computation en double** :
  ```cpp
  const double b_prev1 = static_cast<double>(q1_.getReflectedWave());
  const float a_q1 = static_cast<float>(2.0 * baseDrive1 - b_prev1);
  ```
- **Ic reads en double** : `double Ic1_raw = q1_.getCollectorCurrent();`
- **Tail current constraint en double** : `Ic_sum`, `scale`, `Ic1_`, `Ic2_` tous en double
- **gm en double** : `gm1`, `gm2`, `gm_avg` en double
- **degenFactor en double**
- **dIc (LE deuxième fix critique)** :
  ```cpp
  const double dIc = (Ic1_ - Ic2_) * degenFactor;
  float Vout = static_cast<float>(dIc * static_cast<double>(config_.R_load));
  ```
- **Membres privés** `float → double` :
  ```
  V_bias_base_, Ic_quiescent_  (DC bias)
  Ic1_, Ic2_, Vbe1_, Vbe2_     (output state)
  gm_eff_                       (transconductance)
  ```
  `outputVoltage_` reste `float` (tension mV-V, pas de cancellation)
- **`AcSnap` struct** : `Ic1, Ic2, Vbe1, Vbe2, gm_eff` en `double`
- **Getters** retournent `double` : `getGm()`, `getLocalGain()`, `getIc1()`, `getIc2()`, `getVbe1()`, `getVbe2()`, `getQuiescentIc()`, `getBiasVoltage()`
- **`prepare()`** : `Ic_quiescent_` et `V_bias_base_` calculés en double avec casts explicites

**Impact sur les autres stages :** CEStageWDF, ClassABOutputWDF, EFStageWDF, VASStageWDF, NeveClassAPath assignent les `double` getters à des `float` locaux — conversion implicite, compile sans erreur. Pas de cancellation dans ces stages (pas de soustraction de valeurs quasi-égales).

---

## 3. Fichiers modifiés — Phase 2 (VAS Topology + JE990Path rewrite)

### 3.1 `core/include/core/preamp/VASStageWDF.h` — RÉÉCRITURE COMPLÈTE

**Problème découvert** (vérifié contre je990-2.gif) :
- **Code avait** : `R_collector = 160` (R7), `R_emitter = 130` (R8)
- **Schéma réel** : R7=160Ω est le resistor EMITTER de Q6 (PNP vers +24V). Q6 collector n'a PAS de charge passive — connecté à la sortie du current mirror actif (~60kΩ impedance AC). R8=130Ω est stabilisation base, pas dans le chemin signal.

**Conséquences de l'erreur :**
- Gain VAS code : Av = 160/(130 + 17.2) = **1.09** (FAUX)
- Gain VAS réel : Av = 60000/(160 + 17.2) = **338** (CORRECT)
- Ratio erreur : 338/1.09 = **310×** — c'est pourquoi `backboneGain_ = 500` existait
- Miller pole code : fc = 1/(2π × 160 × 150pF) = **6.6 MHz** (FAUX, 5 décades trop haut)
- Miller pole réel : fc = 1/(2π × 60000 × 150pF) = **17.7 kHz** (CORRECT)

**Nouvelle `VASConfig` :**
```cpp
struct VASConfig {
    BJTParams bjt;
    float R_coll_AC   = 60000.0f;   // Active mirror AC impedance [Ohm]
    float R_emitter   = 160.0f;     // R7 emitter resistor [Ohm]
    float C_miller    = 150e-12f;
    float Vcc         = 24.0f;
    float I_quiescent = 1.5e-3f;
};
```

**Nouveau modèle AC-only** (séparation AC/DC car active load n'a pas de R passif) :
```cpp
// 1. Scatter BJTLeaf (NR solve BE junction)
// 2. AC collector current : Ic_ac = Ic - Ic_quiescent
// 3. AC collector voltage : Vc_ac = -Ic_ac × R_coll_AC
// 4. Emitter degeneration : ×(1/(1 + gm × R_emitter))
// 5. Miller lowpass (dominant pole 17.7 kHz)
// 6. Subtract frozen DC offset
```

Suppression de : `Vc_quiescent_`, `Vc_dc_` (ancien modèle DC par R×I), `Vc_last_`, `getCollectorVoltage()`, `getCollectorDC()`, `getVce()`.

### 3.2 `core/include/core/preamp/JE990Path.h` — RÉÉCRITURE COMPLÈTE

**L'ancien code (Newton solver cassé) a été jeté.** Raisons :
1. `backboneGain_ = 500` causait saturation softSat pendant les probes → Jacobien = 0 → Newton lockait aux rails ±23V
2. `softSat(v3_wdf * 500, Vcc)` écrasait la dynamique
3. Commit avec `beta*yk` (avant le fix double) perdait le signal en float
4. Même après fix double + commit `beta*y`, le backbone rendait les probes inutilisables

**Nouveau JE990Path — architecture :**

```
processSample(input):
    beta = Rg/(Rfb+Rg)
    yk = yPrev

    // 1. Snapshot des 4 stages
    snap = {diffPair, cascode, vas, classAB}

    // 2. evalChain(vfb) : évalue F(input, vfb)
    //    DiffPair → Cascode → VAS → ClassAB
    //    PAS de backbone, PAS de softSat
    //    VAS fournit Av ≈ 338 nativement

    // 3. Deux probes pour Jacobien numérique
    vfb0 = beta*yk + vServo
    y0 = evalChain(vfb0)
    g0 = yk - y0

    y1 = evalChain(vfb0 + beta*eps)    // eps = 1e-4
    g1 = (yk+eps) - y1

    J = (g1-g0)/eps                     // ≈ 1 + T
    if |J| < 0.1 : J = 1               // fallback

    // 4. Newton step
    y = yk - g0/J

    // 5. Commit avec TRUE feedback (beta*y)
    //    Double precision dans DiffPairWDF résout le résidu sub-µV
    restore snap
    evalChain(beta*y + vServo)          // avance les états réactifs

    // 6. DC servo (intégrateur lent ~0.1Hz)
    // 7. Load isolator (post-feedback)
    // 8. C_out HP filter
```

**Supprimé :**
- `backboneGain_ = 500.0f`
- `softSat()` / `softSatDeriv()` (restent dans Constants.h mais plus utilisés ici)
- `wdfAol_` (design-time Aol calculation — plus besoin)
- Anciens commentaires sur "PREDICTED feedback" et float workaround

**Conservé :**
- DC servo (modèle C3 du 990 réel)
- Load isolator post-feedback tap
- C_out HP filter
- Diagnostic printf (conditionnel `diagEnabled_`)

**Config VAS dans prepare() :**
```cpp
VASConfig vasCfg;
vasCfg.bjt         = config_.q6_vas;
vasCfg.R_coll_AC   = 60000.0f;    // Active mirror impedance
vasCfg.R_emitter   = 160.0f;      // R7 (was incorrectly R_collector)
vasCfg.C_miller    = config_.C1_miller;
vasCfg.I_quiescent = 1.5e-3f;
vasCfg.Vcc         = config_.Vcc;
```

---

## 4. Résultats des tests actuels

### 4.1 Build : SUCCÈS (0 erreurs, 0 warnings critiques)

### 4.2 Tests non-JE990 : 25/26 PASS
Seul échec : `jensen_convergence` (test magnétique pré-existant, non lié aux changements preamp).

### 4.3 Test `je990_path` : 16 passed, 5 failed

```
=== DIAG: Newton internals at position 0 ===
  beta=0.319728 Rfb=100 Rg=47 expected_Acl=3.12766
    sample 0: in=0.000000 out=2.584812 ratio=0.0
    sample 1: in=0.000065 out=2.549126 ratio=38975.6
    sample 2: in=0.000131 out=2.513499 ratio=19256.7
    sample 3: in=0.000195 out=2.477936 ratio=12701.5
    sample 4: in=0.000259 out=2.442439 ratio=9436.9

  [S600] in=0.000195 yk=2.358978 y0=2.330939 g0=0.028039 J=1.00 y=2.330910 servo=0.000116

Position 0:  expected= 9.9 dB,  measured= 68.2 dB   FAIL
Position 5:  expected=30.0 dB,  measured= 64.6 dB   FAIL
Position 10: expected=49.9 dB,  measured= 90.2 dB   FAIL

Gain difference pos0→pos10 : 4.9 dB (expected >10 dB)  FAIL
Max |output| : 22.8V (expected <10V)                    FAIL

Tests OK : construction, passthrough, processBlock, reset, Zout, harmonics, load isolator
```

### 4.4 Analyse des échecs

**Symptômes observés :**
1. Output commence à ~2.58V dès sample 0 (input=0) — **DC offset massif**
2. Le ratio out/in décroît mais reste >1000× — le feedback ne mord pas
3. À S600 : `yk=2.36, y0=2.33, g0=0.028, J=1.00` — **Jacobien collapsé à 1.0**
4. Le gain mesuré ~68 dB est proche de Aol théorique (66 dB) — **boucle ouverte**

**Diagnostic :**

Le Newton solver ne converge pas car **J tombe au fallback J=1.0**. Cela signifie que les deux probes donnent presque le même g, donc `(g1-g0)/eps ≈ 0 < 0.1` → J=1.

**Pourquoi J ≈ 0 ?**
Avec Aol ≈ 2086 et eps=1e-4 :
- Le deuxième probe change vfb de `beta × eps = 0.32 × 1e-4 = 3.2e-5 V`
- Le VAS amplifie de 338× — la sortie change de `3.2e-5 × 6.3 × 0.98 × 338 ≈ 6.7V`
- Mais la sortie classAB est clampée à ±Vcc=24V
- Si y0 est déjà proche des rails, les deux probes saturent au même endroit
- Ou bien le VAS lui-même sature (Vc_ac clampé à ±24V dans le code)

**Le problème fondamental :** L'output initial (sample 0, input=0) est déjà à 2.58V au lieu de 0. Il y a un **DC offset résiduel** dès le départ que le settling (100 samples × 0 input) n'élimine pas. Ce DC offset, amplifié par Aol=2086, sature la chaîne.

**Hypothèse racine :** Le VAS AC model `Vc_ac = -(Ic - Ic_q) × R_coll_AC` produit un offset non-nul après warmup car `Ic_quiescent_` (calculé analytiquement) ne correspond pas exactement au `Ic` convergé par le NR. Avec R_coll_AC = 60000, même 1µA d'erreur donne 60mV de DC offset. Et 60mV × Aol_restant ≈ V de DC → sature.

---

## 5. Pistes de résolution pour GPT-5.4

### 5.1 Fix prioritaire : DC offset du VAS
Le `Ic_quiescent_` dans VASStageWDF est calculé comme `sign × I_quiescent` (analytique). Mais le NR solver converge à un Ic légèrement différent après warmup. Avec 60kΩ de gain transimpédance, cette erreur est amplifiée massivement.

**Fix proposé :** Après le warmup de 32 samples, mesurer le Ic réel convergé et l'utiliser comme référence :
```cpp
// Dans VASStageWDF::reset(), après le warmup :
Ic_quiescent_ = bjtLeaf_.getCollectorCurrent();  // NR-converged value
```

### 5.2 Fix secondaire : Jacobien
Avec le DC offset fixé, les probes devraient opérer dans la zone linéaire et J devrait donner ~1+T≈668 (position 0) au lieu de 1.0.

Vérifier aussi que le clamp `Vc_ac = clamp(..., -Vcc, Vcc)` dans VASStageWDF ne tue pas le Jacobien pour des signaux normaux. Avec R_coll_AC=60kΩ, Ic_ac de 0.4mA (variation max plausible) donne Vc_ac = 24V — exactement au clamp. Le clamp devrait être plus large ou remplacé par un softSat.

### 5.3 Stabilité du Newton solver
Si Aol est très grand (2086), le Newton en 1 step depuis un mauvais predictor peut overshooter. Considérer :
- Damping : `y = yk - alpha * g0/J` avec `alpha = 0.8`
- Multi-step : 2-3 iterations Newton au lieu de 1
- Clamp de y à ±Vcc pour éviter les runaway

### 5.4 Test de validation quick
Un bon test unitaire serait d'isoler le VAS seul :
```cpp
VASStageWDF vas;
VASConfig cfg = VASConfig::Q6_Default();
vas.prepare(96000, cfg);
float out = vas.processSample(0.001);  // 1mV input
// Expected: 0.001 × 338 ≈ 0.338V
printf("VAS gain: %.1f (expected ~338)\n", out / 0.001);
```

---

## 6. État des fichiers

| Fichier | Statut | Phase |
|---------|--------|-------|
| `BJTCompanionModel.h` | Modifié (float→double members) | Phase 1 |
| `BJTLeaf.h` | Modifié (double getters, AcState) | Phase 1 |
| `DiffPairWDF.h` | Modifié (double baseDrive, dIc, members) | Phase 1 |
| `VASStageWDF.h` | **Réécrit** (R_coll_AC=60k, AC model) | Phase 2 |
| `JE990Path.h` | **Réécrit** (Newton propre, no backbone) | Phase 2 |
| `test_je990_path.cpp` | Diag loop réduit à 200 samples | Cleanup |

Aucun autre fichier source modifié. Les stages Neve (CEStageWDF, EFStageWDF, NeveClassAPath) compilent correctement avec les double getters (conversion implicite double→float).

---

## 7. Rappel de l'architecture corrigée

```
                    Aol natif ≈ 2086 (66 dB)
                    ┌──────────────────────────────┐
  input ──►(+)──►DiffPair──►Cascode──►VAS──►ClassAB──►─┬──►LoadIsolator──►output
            │     ×6.3      ×0.98    ×338    ×1.0       │
            │   (double)                                 │
            │                                            │
            └──────────────── beta ◄─────────────────────┘
                          Rg/(Rfb+Rg)

  Acl = 1 + Rfb/Rg    (3.1× to 314×, positions 0-10)
  T = beta × Aol       (667 to 6.6, positions 0-10)
```

Double precision active dans : `baseDrive = double(vPlus) + V_bias_base_` et `dIc = (Ic1_ - Ic2_) × degenFactor` (tous en double dans DiffPairWDF).
