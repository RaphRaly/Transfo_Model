# Analyse Rigoureuse JE-990 — KVL/KCL + Diagnostic GPT + Correction Schéma

**Date:** 2026-03-26
**Sources:** Claude Opus analyse + GPT-5.4 diagnostic + schéma original je990-2.gif (Deane Jensen, 1980)
**Fichiers concernés:** VASStageWDF.h, DiffPairWDF.h, BJTLeaf.h, BJTCompanionModel.h, JE990Path.h

---

## Table des Matières

1. [DC Operating Points (KVL/KCL)](#1-dc-operating-points)
2. [Small-Signal AC Gains](#2-small-signal-ac-gains)
3. [Erreur Topologique VAS — Correction depuis le schéma original](#3-erreur-topologique-vas)
4. [Diagnostic GPT — Float Precision dans le Diff Pair](#4-diagnostic-gpt-float-precision)
5. [Loop Gain et Feedback](#5-loop-gain-et-feedback)
6. [Compensation (Miller, Lead)](#6-compensation)
7. [Plan d'Implémentation Corrigé](#7-plan-dimplementation)

---

## 1. DC Operating Points

### 1.1 Tail Current Source (Q4: 2N2484 NPN)

```
KVL: -24V → R3(160Ω) → Q4_E → Q4_C → tail node

I_tail = 3.0 mA (design target, from Jensen spec)
V_E4 = -24 + I_E4 × R3 = -24 + 0.003 × 160 = -23.52V
V_B4 = V_E4 + V_BE4 = -23.52 + 0.65 = -22.87V
I_C4 ≈ I_E4 × α ≈ 3.0 mA
V_CE4 = V_tail - V_E4 = -0.695 - (-23.52) = 22.8V
```

### 1.2 Differential Pair (Q1/Q2: LM-394 NPN matched)

Par symétrie au repos (V_plus = V_minus = 0):

```
I_E1 = I_E2 = I_tail / 2 = 1.5 mA
I_C1 = I_C2 = α × 1.5 mA ≈ 1.5 mA  (β_LM394 = 200)

KVL demi-circuit Q1 (DC, L1 = court-circuit):
  V_B1 - V_BE1 - I_E1 × R1 = V_tail
  0 - 0.65 - 1.5e-3 × 30 = V_tail
  V_tail = -0.695V

V_E1 = V_B1 - V_BE1 = -0.65V
V_C1 = V_E3 (connecté au cascode Q3)
```

### 1.3 Cascode (Q3/Q5: 2N4250A PNP, common-base)

D'après le schéma original je990-2.gif:

```
+24V → R5(300Ω) → Q3 emitter (PNP)
+24V → R4(300Ω) → CR3 → Q3/Q5 base bias
+24V → R6(2K)   → Q5 emitter (PNP)

Q3 collector → Q1 collector (diff pair)
Q5 collector → Q2 collector (diff pair)

Bias Q3/Q5:
  I_R4 via CR3: V_CR3 ≈ 0.65V
  V_B3 = V_B5 ≈ +24 - V_drop_R4_CR3 ≈ 22.9V

Q3 emitter:
  V_E3 = V_B3 + V_EB3 = 22.9 + 0.65 = 23.55V
  V_C3 = V_E3 (≈ same as Q1 collector)
  V_EC3 ≈ 0.45-0.65V (juste au-dessus de saturation — normal pour cascode)

NOTE IMPORTANTE: R5(300Ω) et R6(2K) sont ASYMÉTRIQUES.
  - R5 = 300Ω sur Q3 (côté Q1, -IN)
  - R6 = 2K sur Q5 (côté Q2, +IN)
  Ceci est cohérent avec un miroir de courant actif:
  Q3 est la référence (R5 petit → plus de courant disponible)
  Q5 est la sortie du miroir (R6 grand → le courant est fixé par le miroir)
```

| Param | Q3 | Q5 |
|---|---|---|
| Ic | 1.5 mA | 1.5 mA |
| Vec | ~0.65V | ~0.65V |
| R_emitter | R5 = 300Ω | R6 = 2K |

### 1.4 VAS (Q6: 2N4250A PNP) — CORRIGÉ depuis le schéma original

**CRITIQUE: Le schéma original montre clairement que R7(160Ω) est au EMITTER de Q6, PAS au collecteur.**

```
+24V → R7(160Ω) → Q6 EMITTER (PNP, émetteur vers rail positif)
                   Q6 BASE    ← signal du cascode (via R8=130Ω stabilisation)
                   Q6 COLLECTOR → directement aux bases Q8/Q9 + C2/C3 + C1(Miller)
                   PAS de résistance passive au collecteur.

V_E6 = +24 - I_E6 × R7 = 24 - 1.515e-3 × 160 = 23.76V
V_B6 = V_E6 - V_EB6 = 23.76 - 0.65 = 23.11V
V_C6 = déterminé par le point d'opération de l'étage de sortie (≈ 0V au repos)
V_EC6 = 23.76 - 0 = 23.76V (ample marge avant saturation)

gm_Q6 = I_C6 / V_T = 1.5e-3 / 25.85e-3 = 58.0 mS
```

### 1.5 Output Stage (Q8: MJE-181 NPN, Q9: MJE-171 PNP)

```
Class-AB push-pull, biaisé par CR4 + R10/R11(62K):

I_quiescent = 15 mA (standing current)

Q8 (NPN, upper):
  +24V → Q8_C (collector)
         Q8_B (base) ← VAS output
         Q8_E (emitter) → R13(3.9Ω) → R14(39Ω) → OUTPUT/LOAD
  V_B8 = V_out + V_BE + I_Q × R14 = 0 + 0.65 + 15e-3 × 39 = 1.24V
  V_CE8 = 24 - 0.585 = 23.4V

Q9 (PNP, lower):
  -24V → Q9_C (collector)
         Q9_B (base) ← VAS output (via Q7 pre-driver)
         Q9_E (emitter) → R15(39Ω) → OUTPUT/LOAD
  V_B9 = -1.24V
  V_EC9 = 23.4V

Z_out ≈ r_e + R_sense = 1.72 + 3.9 ≈ 4.8Ω (per side, ‖ ≈ 2.4Ω)
```

### 1.6 Résumé DC

| Transistor | Fonction | Ic | Vce/Vec | Vbe/Veb |
|---|---|---|---|---|
| Q4 (2N2484) | Tail current | 3.0 mA | 22.8V | 0.65V |
| Q1 (LM-394) | Diff pair + | 1.5 mA | 24.2V | 0.65V |
| Q2 (LM-394) | Diff pair - | 1.5 mA | 24.2V | 0.65V |
| Q3 (2N4250A) | Cascode Q1 | 1.5 mA | ~0.65V | 0.65V |
| Q5 (2N4250A) | Cascode Q2 | 1.5 mA | ~0.65V | 0.65V |
| Q6 (2N4250A) | VAS | 1.5 mA | 23.8V | 0.65V |
| Q8 (MJE-181) | Output NPN | 15 mA | 23.4V | 0.65V |
| Q9 (MJE-171) | Output PNP | 15 mA | 23.4V | 0.65V |

---

## 2. Small-Signal AC Gains

### 2.1 Differential Pair (Q1/Q2)

```
gm_raw = Ic / Vt = 1.5e-3 / 25.85e-3 = 58.0 mS
R_emitter_degen = R1 = R2 = 30Ω (chaque côté)

gm_eff = gm / (1 + gm × Re) = 0.058 / (1 + 0.058 × 30) = 0.058 / 2.74 = 21.2 mS

DiffPair calcule Vout = (Ic1 - Ic2) × degenFactor × R_load:
  Av_diff = gm_eff × R_load = 0.0212 × 300 = 6.35

Le code utilise R_load = 300Ω (R4/R5 cascode collector loads). Correct.
```

### 2.2 Cascode (Q3/Q5)

```
Common-base current buffer:
  Ai = α = β/(β+1) = 100/101 = 0.99 (code utilise 0.98)

Le cascode + miroir actif convertit différentiel → single-ended.
Cette conversion est IMPLICITE dans le DiffPairWDF qui calcule déjà Ic1-Ic2.

Av_cascode = -0.98 (inversion physique: ↑Ic → ↑drop sur R4/R5 → ↓Vc)
```

### 2.3 VAS (Q6) — AVANT vs APRÈS correction

#### Code actuel (INCORRECT):

```
R_collector = R7 = 160Ω (FAUX: R7 est à l'émetteur)
R_emitter   = R8 = 130Ω (FAUX: R8 est "VAS stabilisation", pas émetteur)

Av_VAS_code = -R_coll / (R_emit + 1/gm) = -160 / (130 + 17.2) = -1.09
```

#### Réalité physique (CORRECT, depuis le schéma):

```
R7 = 160Ω → ÉMETTEUR de Q6 (vers +24V)
R8 = 130Ω → probablement série base Q6 (stabilisation)
Collecteur Q6 → charge active (impédance de sortie du miroir)

R_emitter_réel = R7 = 160Ω
R_collector_réel = ro_Q6 ‖ Rout_mirror ‖ R_in_output_stage

  ro_Q6 = Vaf / Ic = 50V / 1.5mA = 33.3 kΩ  (2N4250A, Vaf ≈ 50V)
  Rout_cascode_mirror ≈ β × ro = 100 × 33.3k = 3.3 MΩ (très haute)
  R_in_output ≈ β_Q8 × (R14 + r_e) ≈ 5000 × 40.7 ≈ 200 kΩ

  R_coll_eff = ro_Q6 ‖ R_in_output ≈ 33.3k ‖ 200k ≈ 28.5 kΩ

  → Estimation conservatrice: R_coll_AC ≈ 30-80 kΩ

Av_VAS_réel = -R_coll_eff / (R7 + 1/gm)
            = -60000 / (160 + 17.2)
            = -338  (avec R_coll_AC = 60kΩ, à tuner)
```

### 2.4 Output Stage (Q8/Q9)

```
Emitter follower push-pull:
  Av_output ≈ R_L / (R_L + r_e) ≈ 0.95 à 0.97
  Z_out ≈ 4.8Ω

Le code utilise Av ≈ 0.95. Correct.
```

### 2.5 Comparaison Gains — Code vs Réalité

| Étage | Gain WDF (code) | Gain Physique | Ratio | Source erreur |
|---|---|---|---|---|
| DiffPair | +6.35 | +6.35 | 1x | OK |
| Cascode | −0.98 | −0.98 | 1x | OK |
| **VAS** | **−1.09** | **−338** | **~310x** | **R7/R8 inversés + pas de charge active** |
| ClassAB | +0.97 | +0.97 | 1x | OK |
| **Aol total** | **6.7** | **~2000-3500** | **~500x** | backbone compense |

**Le `backboneGain_ = 500` compense exactement l'erreur topologique du VAS.**

---

## 3. Erreur Topologique VAS — Correction depuis le schéma original

### 3.1 Ce que le schéma original (je990-2.gif) montre

En traçant les connexions de Q6 sur le schéma original:

```
                    +24V
                      |
                 R7 [160Ω]  ← EMITTER resistor (bias + dégénération)
                      |
                 Q6 EMITTER (2N4250A PNP)
                 Q6 BASE ← signal cascode (via R8=130Ω stabilisation)
                 Q6 COLLECTOR
                      |
                 ┌────┴────┬──────────────┐
                 |         |              |
            C1[150pF]   C2[62pF]    vers Q8/Q9
            (Miller →    C3[91pF]    bases (output)
             diff pair   (lead comp
             input)       → output)
                 |
          PAS de R vers -24V ou +24V au collecteur!
```

### 3.2 Ce que le code modélise (INCORRECT)

```cpp
// VASStageWDF.h, commentaire lignes 12-22:
//                       R7 [160 Ohm] (collector load)    ← FAUX
//                            |C
//  From cascode --> Q6 Base (2N4250A PNP)
//                            |E
//                       R8 [130 Ohm] (emitter resistor)  ← FAUX

// VASConfig:
float R_collector = 160.0f;    // R7 — ASSIGNÉ AU COLLECTEUR, devrait être à l'émetteur
float R_emitter   = 130.0f;    // R8 — ASSIGNÉ À L'ÉMETTEUR, est probablement série base

// Gain calculation (ligne 276):
float Vc = sign_ * config_.Vcc - Ic * config_.R_collector;
// Ceci calcule: Vc = -24 + |Ic| × 160 — charge passive 160Ω au collecteur
```

### 3.3 Impact

1. **Gain VAS** : 1.09x au lieu de ~338x → nécessite backbone × 500
2. **Pôle dominant Miller** : `fc = 1/(2π × 160 × 150pF) = 6.6 MHz` au lieu de `1/(2π × 60kΩ × 150pF) = 17.7 kHz` → **faux de 5 décades**
3. **Slew rate** : Le Miller cap voit 1/500ème du swing réel → slewing mal modélisé
4. **Dynamique large signal** : Le softSat après backbone ne capture pas correctement le clipping du VAS réel

### 3.4 Fix proposé

Remplacer la charge passive par un modèle AC/DC séparé:

```cpp
// NOUVEAU VASConfig:
float R_emitter    = 160.0f;    // R7: émetteur vers +24V (dégénération)
float R_base_series = 130.0f;   // R8: série base (stabilisation, optionnel)
float R_coll_AC    = 60000.0f;  // Impédance AC du miroir actif (ro_Q6 ‖ Rout_mirror)
                                 // PAS une résistance physique — à tuner vs mesure/SPICE

// NOUVEAU processSample():
// DC: Vc_dc fixé au point de repos (inchangé)
// AC: Vc = Vc_dc + (Ic - Ic_quiescent) × R_coll_AC
float Vc = Vc_dc_ + (Ic - Ic_quiescent_) * config_.R_coll_AC;

// Miller pole corrigé:
// fc = 1/(2π × R_coll_AC × C_miller) = 1/(2π × 60k × 150pF) = 17.7 kHz
// Après Miller multiplication: fc_eff = 17.7k / (1 + |Av_VAS|) ≈ 52 Hz
```

---

## 4. Diagnostic GPT — Float Precision dans le Diff Pair

### 4.1 Le problème (résumé GPT-5.4)

> "Le point bloquant est la disparition de `e = vPlus - vMinus` quand les deux branches
> du diff pair sont reconstruites comme `0.63V + petit signal`, puis re-quantifiées
> en `float`. Le Newton calcule une bonne solution algébrique, mais la chaîne WDF
> ne peut pas la confirmer au commit."

### 4.2 Vérification numérique (IEEE 754 float32)

Pour un signal de 1 mV en entrée, avec T = 106.6 (loop gain):

```
V_bias_base = 0.6965V (Vt × ln(Ic_q/Is + 1))
error = input / (1+T) = 1e-3 / 107.6 = 9.3 µV

baseDrive1 = 0.001000 + 0.696474 = 0.697474V  (float32: 0x3F328DA1)
baseDrive2 = 0.000991 + 0.696474 = 0.697464V  (float32: 0x3F328D05)

Différence = 9.298e-6V = 156 ULPs → 7 bits de précision restants
```

| Signal entrée | Erreur diff pair | Bits float32 | Verdict |
|---|---|---|---|
| 1 mV | 9.3 µV | **7 bits** | Marginal |
| 100 µV | 0.93 µV | **3 bits** | Dégradé |
| 10 µV | 93 nV | ~0 bits | **Perdu** |
| 1 µV | 9.3 nV | 0 bits | **Totalement détruit** |

**Seuil critique: ~6.4 µV d'entrée.** En dessous, `baseDrive1` et `baseDrive2` arrondissent au même float32.

### 4.3 Verdict sur le diagnostic GPT

**PARTIELLEMENT CORRECT.**

#### Ce que GPT a vu juste:
1. La cancellation catastrophique en float est RÉELLE et quantifiable
2. La recommandation `double` end-to-end est correcte et prioritaire
3. L'observation que le commit ne peut pas utiliser le vrai feedback (beta×y) en float est correcte

#### Ce que GPT a dit de FAUX:
1. **"yPrev_ n'est jamais recollé"** → FAUX. Ligne 305: `yPrev_ = y` le fait correctement.
2. **"Retour au vrai commit avec beta×y"** → IMPOSSIBLE en float32. `beta×y ≈ input` donne erreur ~86 nV = 0.001 ULP. Le commit avec `beta×yk` (erreur ~9.3 µV = 156 ULP) est le seul viable en float.
3. **Implique échec total** → En réalité, fonctionne pour signaux > 6.4 µV (couvre la plupart de l'audio), mais avec seulement 7 bits de résolution différentielle.

#### Ce que GPT a MANQUÉ:
1. `Vbe_prev_ = static_cast<float>(Vbe)` (BJTCompanionModel.h:106) — le warm-start NR perd la différence Vbe sub-ULP entre Q1 et Q2
2. `Ic_last_`, `Ib_last_` en float (BJTCompanionModel.h:113-114) — la soustraction `Ic1 - Ic2` subit la même cancellation
3. Le pôle dominant Miller est faux de 5 décades (lié à l'erreur VAS, pas au float)

### 4.4 Formulation vcm/vdiff recommandée par GPT

```cpp
double vcm   = 0.5 * (vPlus + vMinus);
double vdiff =        (vPlus - vMinus);   // signal d'erreur préservé!

double base1 = V_bias + vcm + 0.5 * vdiff;
double base2 = V_bias + vcm - 0.5 * vdiff;
```

**OPTIONNEL si double est utilisé partout.** En double, vdiff a ~36 bits de précision même pour un signal de 1 mV. La décomposition vcm/vdiff n'apporte un bénéfice que si on veut rester en float32.

---

## 5. Loop Gain et Feedback

### 5.1 Feedback factor

```
Configuration non-inverseuse:
  β = Rg / (Rfb + Rg)

Défaut code: Rfb = 1430Ω, Rg = 47Ω
  β = 47 / 1477 = 0.0318
  Acl_ideal = 1 + Rfb/Rg = 1 + 1430/47 = 31.4 (+29.9 dB)
```

### 5.2 Loop gain

```
T = Aol × β

Avec Aol_eff = 3350 (backbone inclus):
  T = 3350 × 0.0318 = 106.6 (+40.6 dB)

Acl = Aol / (1 + T) = 3350 / 107.6 = 31.1  (erreur vs idéal: 0.95%)
```

### 5.3 Seuil de validité

```
T > 10 requis pour Acl ≈ 1 + Rfb/Rg (erreur < 10%)

T = 10 quand: Acl_max = Aol / 11 = 3350 / 11 = 305 (+49.7 dB)

Au-delà de ~50 dB de gain fermé, le loop gain devient insuffisant.
C'est pourquoi le Jensen Twin Servo utilise 2× JE-990 en cascade.
```

### 5.4 Table de gain aux positions clés

| Pos | Rfb (Ω) | Acl idéal | Acl (dB) | β | T | Erreur |
|---|---|---|---|---|---|---|
| 0 | 47 | 2.0 | 6.0 | 0.500 | 1675 | 0.06% |
| 3 | 235 | 6.0 | 15.6 | 0.167 | 559 | 0.18% |
| 5 | 470 | 11.0 | 20.8 | 0.091 | 305 | 0.33% |
| 7 | 1430 | 31.4 | 29.9 | 0.032 | 107 | 0.95% |
| 10 | 14700 | 314 | 49.9 | 0.003 | 10.5 | 9.5% |

---

## 6. Compensation

### 6.1 C1 = 150 pF — Miller Compensation (pôle dominant)

**Code actuel (FAUX):**
```
fc = 1/(2π × R_collector × C1) = 1/(2π × 160 × 150e-12) = 6.63 MHz
```

**Réalité (CORRECT, après fix VAS):**
```
fc_raw = 1/(2π × R_coll_AC × C1) = 1/(2π × 60kΩ × 150pF) = 17.7 kHz

Miller multiplication: C_eff = C1 × (1 + |Av_VAS|) = 150pF × 339 = 50.9 nF
fc_dominant = fc_raw / (1 + |Av_VAS|) ≈ 17.7kHz / 339 ≈ 52 Hz

Unity-gain frequency:
  f_unity = gm_VAS / (2π × C1) = 58e-3 / (2π × 150e-12) = 61.5 MHz
```

### 6.2 C2 = 62 pF, C3 = 91 pF — Lead Compensation

```
C2 entre VAS collector et Q8 collector (NPN upper output)
C3 entre VAS collector et Q9 area (PNP lower output)

Fournissent un zéro de phase pour compenser le 2ème pôle de l'étage de sortie.
Fréquences bien au-dessus de l'audio (> 500 MHz).
```

### 6.3 Phase Margin

```
2ème pôle (output stage):
  f_p2 = gm_output / (2π × C_parasitic) ≈ 0.58S / (2π × 50pF) = 1.85 GHz

Phase margin:
  φ = 90° - arctan(f_unity / f_p2) = 90° - arctan(61.5M/1.85G) = 88°

→ Excellente stabilité. Le JE-990 est conçu pour charger des câbles capacitifs.
```

---

## 7. Plan d'Implémentation Corrigé

Deux problèmes indépendants, traités en 2 phases + 1 phase optionnelle.

### Phase 1 — Précision Double (résout le problème float GPT)

**Objectif:** Éliminer la cancellation catastrophique dans le diff pair.

**Fichiers à modifier:**

#### 1.1 BJTCompanionModel.h
- Passer les membres `Vbe_prev_`, `Ic_last_`, `Ib_last_`, `gm_` de `float` → `double`
- La méthode `solve()` retourne `double` au lieu de `float`
- Supprimer les `static_cast<float>` de retour (lignes 106, 113-114)
- `getCollectorCurrent()`, `getBaseCurrent()`, `getTransconductance()` retournent `double`

#### 1.2 BJTLeaf.h
- `a_incident_`, `b_reflected_` de `float` → `double`
- `scatterImpl()` prend et retourne `double`
- `Z_port_` reste `float` (fixé à configure, pas de précision critique)

#### 1.3 DiffPairWDF.h
- `processSample()` variables internes en `double` (baseDrive, a_q, Ic, dIc, Vout)
- Membres `Ic1_`, `Ic2_`, `Vbe1_`, `Vbe2_`, `gm_eff_`, `outputVoltage_` en `double`
- Interface publique reste `float` (retour de processSample)
- Optionnel: décomposition vcm/vdiff pour précision supplémentaire

#### 1.4 JE990Path.h — Commit avec vrai feedback
- APRÈS le passage en double, le commit peut utiliser `beta×y` (le vrai feedback)
- `vfb_commit = beta * y + vServo_` au lieu de `beta * yk + vServo_`
- Critère de succès: `|yCommit - y| < 1e-10` (vs actuel `|yCommit - y| >> y`)
- Supprimer le commentaire expliquant pourquoi on ne peut pas utiliser beta×y

### Phase 2 — Correction Topologie VAS (résout l'erreur de gain + fréquence)

**Objectif:** Éliminer le backbone × 500, corriger la réponse en fréquence.

**Fichiers à modifier:**

#### 2.1 VASStageWDF.h — Refonte du modèle VAS

```cpp
// AVANT (INCORRECT):
struct VASConfig {
    float R_collector = 160.0f;   // R7 — FAUX: c'est l'émetteur
    float R_emitter   = 130.0f;   // R8 — FAUX: c'est stabilisation base
};

// APRÈS (CORRECT):
struct VASConfig {
    float R_emitter     = 160.0f;    // R7: émetteur Q6 → +24V (dégénération)
    float R_coll_AC     = 60000.0f;  // Impédance AC collecteur (miroir actif)
                                      // ≈ ro_Q6 ‖ R_in_output ≈ 30-80 kΩ
                                      // Tuner vs mesure ou SPICE
    float C_miller      = 150e-12f;  // C1 Miller compensation [F]
    // ...
};
```

#### 2.2 VASStageWDF::processSample() — Modèle AC/DC séparé

```cpp
// AVANT:
float Vc = sign_ * config_.Vcc - Ic * config_.R_collector;

// APRÈS:
// DC fixé au point de repos. AC via impédance active.
float Vc = Vc_dc_ + (Ic - Ic_quiescent_) * config_.R_coll_AC;
// Note: le signe est déjà correct car pour PNP, dIc < 0 quand Vc augmente
```

#### 2.3 Miller pole corrigé

```cpp
// AVANT:
const float fc = 1.0f / (kTwoPif * config.R_collector * config.C_miller);
// → fc = 6.6 MHz (FAUX)

// APRÈS:
const float fc = 1.0f / (kTwoPif * config.R_coll_AC * config.C_miller);
// → fc = 1/(2π × 60k × 150p) = 17.7 kHz (CORRECT)
// Après Miller multiplication: ~52 Hz pôle dominant
```

#### 2.4 JE990Path.h — Supprimer le backbone

```cpp
// AVANT:
float backboneGain_ = 500.0f;
const float v3 = softSat(v3_wdf * backboneGain_, config_.Vcc);

// APRÈS:
// Plus besoin de backbone — le VAS produit le bon gain directement
const float v3 = softSat(v3_wdf, config_.Vcc);
// (softSat conservé pour modéliser le clipping aux rails)
```

#### 2.5 JE990Path::prepare() — Recalculer wdfAol_

```cpp
// AVANT:
const float vasGain = 160.0f / (130.0f + 1.0f / gm_vas);  // = 1.09

// APRÈS:
const float vasGain = config_.R_coll_AC / (config_.R_emitter_vas + 1.0f / gm_vas);
// = 60000 / (160 + 17.2) = 338
```

### Phase 3 — Optionnelle (vcm/vdiff + tuning)

#### 3.1 Décomposition vcm/vdiff dans DiffPairWDF

```cpp
// Si précision >140 dB requise:
double vcm   = 0.5 * (vPlus + vMinus);
double vdiff =        (vPlus - vMinus);
double base1 = V_bias + vcm + 0.5 * vdiff;
double base2 = V_bias + vcm - 0.5 * vdiff;
```

#### 3.2 Tuning de R_coll_AC

Le paramètre R_coll_AC (impédance AC du collecteur VAS) n'est PAS une résistance physique mesurable. C'est l'impédance effective vue au collecteur Q6, déterminée par:

```
R_coll_AC = ro_Q6 ‖ Rout_mirror ‖ R_in_output

Estimation: 30 kΩ à 80 kΩ selon:
  - Vaf de Q6 (2N4250A): 50-100V → ro = 33-67 kΩ
  - Topologie exacte du miroir (simple, Wilson, cascode)
  - Beta du pré-driver (Q7)
```

**Méthode de tuning:**
1. Mesurer l'Aol du vrai JE-990 (si dispo) ou simuler en SPICE
2. Aol_mesuré = gm_eff_diff × R_load × α × (R_coll_AC / (R7 + 1/gm_VAS)) × Av_output
3. Résoudre pour R_coll_AC
4. Vérifier la réponse en fréquence open-loop (dominant pole ~20-100 Hz attendu)

### Ordre d'implémentation recommandé

```
Phase 1 (float → double):
  1.1  BJTCompanionModel.h  — membres + retours en double
  1.2  BJTLeaf.h            — wave variables en double
  1.3  DiffPairWDF.h        — processSample internals en double
  1.4  Compiler + vérifier que les 22 tests passent encore
  1.5  JE990Path.h commit   — switcher vers beta×y
  1.6  Revalider gain fermé positions 0, 5, 10

Phase 2 (VAS topology fix):
  2.1  VASConfig             — renommer champs, ajouter R_coll_AC
  2.2  VASStageWDF           — modèle AC/DC séparé + Miller corrigé
  2.3  JE990Path             — supprimer backboneGain_
  2.4  Compiler + tests
  2.5  Tuner R_coll_AC pour matcher Aol_cible ≈ 3350 (70 dB)
  2.6  Vérifier réponse en fréquence open-loop

Phase 3 (optionnel):
  3.1  vcm/vdiff dans DiffPairWDF (si nécessaire)
  3.2  Fine-tuning R_coll_AC vs SPICE
```

---

## Annexe A: Schéma Original vs Code — Table de Correspondance

| Composant | Schéma original (je990-2.gif) | Code actuel | Correct? |
|---|---|---|---|
| R1 = 30Ω | Q1 emitter degen | DiffPairConfig R_emitter = 30 | OK |
| R2 = 30Ω | Q2 emitter degen | DiffPairConfig R_emitter = 30 | OK |
| R3 = 160Ω | Q4 tail → -24V | DiffPairConfig R_tail = 160 | OK |
| R4 = 300Ω | +24V → CR3 bias Q3/Q5 bases | Non modélisé (bias implicite) | OK |
| R5 = 300Ω | +24V → Q3 emitter (PNP) | CascodeConfig R_load = 300 | OK (equiv.) |
| R6 = 2K | +24V → Q5 emitter (PNP) | Non modélisé séparément | ~OK |
| **R7 = 160Ω** | **+24V → Q6 EMITTER** | **VASConfig R_collector = 160** | **FAUX** |
| **R8 = 130Ω** | **VAS stabilisation (série base?)** | **VASConfig R_emitter = 130** | **FAUX** |
| R10 = 62K | Output bias | ClassABConfig (implicite) | OK |
| R11 = 62K | Output bias | ClassABConfig (implicite) | OK |
| R13 = 3.9Ω | Output current sense | ClassABConfig R_sense = 3.9 | OK |
| R14 = 39Ω | Q8 emitter degen | ClassABConfig R_emitter_top = 39 | OK |
| R15 = 39Ω | Q9 emitter degen | ClassABConfig R_emitter_bottom = 39 | OK |
| C1 = 150pF | Miller VAS→diff input | VASConfig C_miller = 150pF | OK (valeur) |
| C2 = 62pF | Lead comp output top | ClassABConfig C_comp_top = 62pF | OK |
| C3 = 91pF | Lead comp output bottom | ClassABConfig C_comp_bottom = 91pF | OK |
| L1 = 20µH | Q1 emitter Jensen inductor | DiffPairConfig L_emitter = 20µH | OK |
| L2 = 20µH | Q2 emitter Jensen inductor | DiffPairConfig L_emitter = 20µH | OK |
| L3 = 40µH | Output load isolator | LoadIsolatorConfig L_series = 40µH | OK |

## Annexe B: Texte complet du diagnostic GPT-5.4

> Oui : vous avez maintenant quitté le problème de polarité et vous êtes arrivé au vrai mur
> restant, qui est un problème de **résolution numérique** dans le diff pair, pas un problème
> de topologie. Le fait que le premier sample tombe près du bon ratio montre que le Newton et
> la structure de boucle sont globalement bons, mais que le commit physique ne "voit" plus le
> signal d'erreur quand β·y ≈ x.
>
> **Diagnostic:** Le point bloquant est bien la disparition de e = vPlus - vMinus quand les
> deux branches du diff pair sont reconstruites comme `0.63V + petit signal`, puis
> re-quantifiées en `float`.
>
> **Recommandations GPT (dans l'ordre):**
> 1. `double` end-to-end dans BJTLeaf et BJTCompanionModel
> 2. Diff pair réécrit en coordonnées vcm/vdiff
> 3. Retour au vrai commit avec beta×y + vServo, pas beta×yk
> 4. Revalider gain fermé aux positions 0, 5 et 10
>
> **Critère de succès (GPT):** "Votre critère de succès n'est pas seulement 'le Newton donne
> le bon y', mais 'le commit redonne pratiquement le même y'. Tant que |yCommit - y| reste
> grand en petit signal, vous êtes encore limité par la précision numérique du diff pair."

## Annexe C: Références

- Jensen, D. (1980). JE-990 Discrete Operational Amplifier. Schéma je990-2.gif.
- Hardy, J. M-990/990C Twin Servo Microphone Preamplifier. johnhardyco.com/pdf/990.pdf
- Werner, K. J. (2016). Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters. PhD Thesis, CCRMA, Stanford.
- Bernardini, A., Werner, K. J., Smith, J. O., Sarti, A. (2020). "Generalized Wave Digital Filter Realizations of Arbitrary Reciprocal Connection Networks." IEEE TASLP.
- Ebers, J. J. & Moll, J. L. (1954). "Large-Signal Behavior of Junction Transistors." Proc. IRE.
