# Sprint 2-4 — Notes théoriques préalables

**Date :** 2026-04-26 (avant le démarrage Sprint 2)
**Source :** 3 recherches parallèles (sandbox, sans WebSearch/WebFetch — dérivations
canoniques uniquement, DOI à reverifier ultérieurement). Voir aussi
`docs/archive/ACADEMIC_RESEARCH_2026-03-27.md` qui couvre déjà Sprint 3 partiellement
et le hScale flux-integrator (Sprint 2 base).

**Objectif :** identifier les hypothèses de l'audit qui ne tiennent pas la
dérivation, valider celles qui tiennent, et donner les formules concrètes que
Sprints 2/3/4 doivent utiliser.

> **Caveat global :** WebSearch/WebFetch refusés en sandbox. Les agents ont
> travaillé de mémoire canonique (Jiles 1986/1992, Bertotti 1988, Vaidyanathan
> 1993, Kaiser 1974, Baghel 2014, etc.). Les équations sont vérifiables ; les
> DOIs sont à reconfirmer en ligne avant publication.

---

## Mise à jour empirique — 2026-04-26 (post-Sprint 1) ⚡

Deux diagnostics exécutés avant tout fix Sprint 2/3, avec invalidations
fortes des plans initiaux.

### Diag-A (Sprint 3 simplification one-liner) — INVALIDÉ

Patch testé : enlever `&& config_.calibrationMode != CalibrationMode::Physical`
de `TransformerModel.h:145-146` pour activer `computeFieldSeparated` en mode
Physical. Re-run `validate_jensen` → output `data/measurements/diag_sprint3_no_guard/`.

| Point JT-115K-E Physical | Baseline THD | Patché THD | Δ |
|---|---|---|---|
| 20 Hz / -20 dBu | 1.1506 % | 1.1507 % | < 0.0001 % |
| 20 Hz / -2.5 dBu | 1.1453 % | 1.1453 % | 0 |
| 20 Hz / +1.2 dBu | 1.1451 % | 1.1451 % | 0 |
| 1 kHz / -20 dBu | 0.0212 % | 0.0208 % | -0.0004 % |
| 1 kHz / +4 dBu | 0.0205 % | 0.0204 % | -0.0001 % |

Le wedge à 1.15 % indépendant du niveau **persiste**. Conclusion : la cause
n'est pas le manque de field-separation (la correction `K1·dB/dt + K2·√|dB/dt|`
est négligeable à 20 Hz / faible amplitude). La pathologie vient d'ailleurs —
hypothèse forte = transient HP filter / NR warm-up dominant la fenêtre
d'analyse (observation #1 du Sprint 1 report). La garde a été **revertée**
puisque le patch n'apporte rien.

### Diag-B (Sprint 2 χ_diff) — AUDIT INVALIDÉ, code DÉJÀ correct

Outil : `tools/diag_chi_diff.cpp` (added to `Tests/CMakeLists.txt`). Pilote
`HysteresisModel<LangevinPade>` direct avec H₀·sin(2π·50·t), 8 cycles, mesure
χ_diff = `getInstantaneousSusceptibility()` au passage à zéro de M dans le
dernier cycle. Sweep H₀ de 0.05·a à 4·a.

**Découverte mathématique cruciale** : `chiEff_code` ≡ `chiMinJiles`.

```
chiEff_code = chi0_c / (1 - α·chi0_c)         avec chi0_c = Ms·c/(3a)
chiMinJiles = c·chi0_an / (1 - α·c·chi0_an)   avec chi0_an = Ms/(3a)
            = (c·Ms/(3a)) / (1 - α·c·Ms/(3a))
            = chi0_c / (1 - α·chi0_c)
            = chiEff_code   ✓ identité algébrique
```

Le code utilise donc **déjà** la formule χ_minor de Jiles 1992. Le commentaire
ligne 519 (« χ_eff = χ₀ / (1 − α·χ₀) ») est trompeur — il s'écrit comme la
formule anhystérétique, mais avec χ₀ pré-multiplié par c, c'est mathématiquement
χ_minor.

**Mesures empiriques** :

| Preset | Code chiEff | χ@M=0 mesuré (H₀=0.05a → 4a) | Ratio mesuré/code | Ratio mesuré/chiAnEff |
|---|---|---|---|---|
| JT-115K-E | 10 809 | 10 809 → 10 833 | 1.000 → 1.002 | **0.688** (mu-metal) |
| JT-11ELCF | 9 527 | 9 527 → 10 035 | 1.000 → 1.053 | **0.414** |

Le ratio 0.688 mu-metal correspond exactement à la prédiction théorique 0.687
du §2.2 ci-dessous. La déviation à fort H₀ (×1.05 pour ELCF à 4·a) vient du
terme irréversible non-nul quand M=0 ne coïncide plus avec M_an=0 (coercivité
plus marquée à fort drive).

**Conclusion Sprint 2** : `bNorm` est déjà calibré contre la susceptibilité
réellement produite par le J-A en steady-state. **L'audit a inversé la
comparaison** : il a comparé `chiEff_code` à `chiAnEff` au lieu de comparer
au χ produit en pratique. Le +4 dB excess gain mesuré au test
`testJensen_JT115KE_Gain_Physical` n'est pas un problème de bNorm. Il faut
chercher ailleurs (suspects : intégrateur de flux non-flat à f≠f_ref dans
Physical, hScale = a·5 trop agressif en Artistic, post-J-A signal chain).

### Implications pour le plan d'attaque

| Sprint plan original | Statut après diagnostic | Nouvelle direction |
|---|---|---|
| Sprint 2 : recalibrer bNorm via χ_minor | INVALIDÉ — déjà fait | Investiguer cause réelle du +4 dB |
| Sprint 3 : enlever la garde Physical (one-liner) | INVALIDÉ — pas d'effet | Plan B: forward-NR avec Jacobien §3.4, OU diagnostic plus poussé du wedge 20 Hz |
| Sprint 4 : polyphase halfband | INTACT — analyse théorique solide | Procéder selon §4.2-4.6 |

Les fichiers `tools/diag_chi_diff.cpp` et `data/measurements/diag_sprint3_no_guard/`
sont conservés comme régression checks futurs.

---

## Sprint 2 — Recalibration hScale / bNorm

### 2.1 hScale (déjà couvert)

L'audit propose `hScale = N / (2π·f_ref·Lm·l_e)` (Ampère + magnetizing
current). Le code actuel `core/include/core/model/TransformerModel.h` ligne
489-510 implémente déjà cette formule **quand `calibrationMode == Physical`**.
Pas de question théorique pendante — c'est juste l'activer par défaut sur
les presets Jensen (sprint plan §2 plan 3).

Réf. principale: Giampiccolo *et al.* 2021 + Holters/Zölzer DAFx-16, déjà
documentés dans `ACADEMIC_RESEARCH_2026-03-27.md` §4.

### 2.2 bNorm — l'audit se trompe probablement de signe ⚠️

**Affirmation audit** (sprint plan §2 root cause) :
> "bNorm utilise chiEff anhystérétique alors que la susceptibilité minor-loop
> J-A est ~65 % supérieure → +4 dB d'erreur de gain unitaire en linéaire."

**Dérivation depuis Jiles 1992 Eq. 8** (canonical, pas de doute) :

À M=0 sur la branche montante d'une mineure loop symétrique en régime
établi, M_irr = M_an = 0 donc le terme irréversible (M_an − M_irr)/(δ·k) **s'annule** :

```
χ_minor = c · χ₀ / (1 − α · c · χ₀)
χ_eff   = χ₀ / (1 − α · χ₀)            (formule actuelle du code)

ratio = χ_minor / χ_eff = c · (1 − α·χ₀) / (1 − α·c·χ₀)
```

**Application numérique mu-metal (JT-115K-E)** : Ms=5.5e5, a=30, c=0.85, α=1e-4.

```
χ₀         = Ms/(3a) = 6 111
α·χ₀       = 0.611
χ_eff      ≈ 15 720
χ_minor    = 0.85·6 111 / (1 − 1e-4·0.85·6 111) ≈ 10 810
χ_minor / χ_eff ≈ 0.687     →  −3.3 dB (pas +4 dB)
```

Pour 50 % NiFe (Ms=8e5, a=80, c=0.9, α=5e-5) : ratio ≈ 0.883 → −1.1 dB.

**Conclusion :** la susceptibilité minor-loop est **plus petite** que χ_eff (le
facteur audit a le signe opposé). Le test `testJensen_JT115KE_Gain_Physical`
mesure pourtant +4 dB de gain. Causes possibles à investiguer **avant
d'appliquer un fix bNorm** :

1. **Virgin-state transient** : RMS calculé sur le 1er quart de cycle depuis
   M=0 → χ_inst grimpe au-dessus de χ_minor stationnaire. Mitigation : garder
   le warmup étendu (131072 samples) déjà en place.
2. **δ-handling à dH/dt=0** : `sign(dH/dt)` indéfini aux extrema. Si δ flip à
   chaque sample, χ apparente gonfle ≈(1+(1−c)/c) ≈ 1.18 pour c=0.85
   (coïncidence troublante avec le facteur audit). Vérifier `HysteresisModel.h`.
3. **Negative differential susceptibility (Zirka 2012)** près des reversals :
   `dM_irr/dHe < 0` peut renverser le signe du dénominateur — pathologie
   connue. Clamp Deane/Bergqvist `dM_irr/dHe ≥ 0` recommandé.

**Action Sprint 2 :** **avant** de toucher à `bNorm`, refaire la mesure de
gain unitaire en isolant ces 3 effets (test diagnostic dédié). Si le +4 dB
disparaît avec un δ stable et un warmup correct, **garder χ_eff**. Si le +4
dB persiste, investiguer une cause autre que le ratio χ_minor/χ_eff (par
exemple, l'intégration trapézoïdale du flux ou le HP filter prewarp).

**Si fix bNorm finalement justifié**, formule à utiliser :

```cpp
const double chi0    = Ms * c / (3.0 * a);
const double chiMin  = c * chi0 / (1.0 - alpha * c * chi0);   // minor-loop
const double bNorm   = 1.0 / (mu0 * (1.0 + chiMin) * hScale); // pas chiEff
```

**Refs.** Jiles & Atherton, *JMMM* 61 (1986) 48 ; Jiles, *J. Appl. Phys.* 76
(1994) 5849 ; Sablik & Jiles, *IEEE TMag* 29 (1993) 2113 ; Zirka *et al.*,
*J. Appl. Phys.* 112 (2012) 043916 ; Pop & Caltun, *Acta Phys. Pol. A* 120
(2011) 491.

### 2.3 CMA-ES sur courbes THD vs niveau

Pas de gap théorique majeur. Le pipeline `identification/CMA_ES.h` existe.
Recommandations standard pour l'objective function :

- **Échelle log** sur THD% (différence en dB plutôt que linéaire).
- **Floor numérique** pour gérer les datasheet `<0.001 %` (clip à 1e-3 %).
- **Weighting** datasheet vs measured uniforme (pas de pondération
  ad hoc) — sinon le fit triche sur les points faciles.
- **Borner les paramètres** par les valeurs nominales mu-metal/NiFe ±50 %
  pour éviter dérives non-physiques (Ms, a, k positifs ; α ∈ [0, 1e-3] ;
  c ∈ [0, 1]).

---

## Sprint 3 — Bertotti dans le NR (peut-être pas comme l'audit le propose) ⚠️

### 3.1 Hodgdon 1988 — n'est pas une recette de couplage NR

Le sprint plan cite « Hodgdon 1988, Fuzi 2004 pour le couplage NR
rate-dependent ». **Hodgdon 1988** (IEEE TMag vol. 24 n°1 p. 218-221, DOI
10.1109/20.43893, et son compagnon DOI 10.1109/20.43892) propose en réalité
un **modèle ODE alternatif au J-A** :

```
dH/dt = α · sign(dB/dt) · [f(B) − H] + g(B) · dB/dt
```

C'est rate-independent dans sa forme originale ; il fournit un cadre de
**well-posedness** (existence/unicité) pour ODEs pilotées par sign(dB/dt),
pas une méthode de couplage Bertotti. La citation du sprint est conceptuelle.

### 3.2 Fuzi 2004 — référence ambiguë

Plusieurs candidats Fuzi 2004 (BME Hungary). Le plus plausible vu le
contexte : **Füzi & Iványi, "Features of two rate-dependent hysteresis
models", *Physica B* 343 (2004) 173-181**. Ce papier *fait* du couplage
Newton trapézoïdal pour un terme dynamique style Bertotti. **DOI à confirmer
en ligne** quand WebSearch sera dispo.

### 3.3 Field-separation (Baghel 2014) vs NR-couplé : forte recommandation

**Le sprint propose forward-mode NR couplé** :

```
g(H) = R_JA(H − K1·u(H) − K2·sign(u)·√|u|)     u = (B_JA(H) − B_committed)/Ts
```

**Or Baghel & Kulkarni 2014** ("Dynamic Loss Inclusion in J-A...", IEEE TMag,
DOI 10.1109/TMAG.2013.2284381) **prouvent que cette approche "original JA"
est instable** (lengthening vertical = ton sign-flip actuel). Ils recommandent
l'inverse : **field-separation inverse-mode** :

```
B input → H_stat = J-A_inverse(B)
H_eddy = K1 · dB/dt
H_excess = K2 · sign(dB/dt) · √|dB/dt|
H_output = H_stat + H_eddy + H_excess     (additif, pas de boucle implicite)
```

| Critère | Forward-NR (sprint) | Field-separation (Baghel) |
|---|---|---|
| Coût | NR avec Jacobien dynamique, 5-8 iter | NR statique J-A + 2 mults + sqrt |
| Stabilité | Conditionnelle, sign-flip à bas H | Inconditionnelle |
| Singularité √\|u\| | Régularisation requise | Pas de singularité (additif) |
| Audio temps-réel | OK si convergence | Strict — déjà éprouvé |

**Observation cruciale du codebase :** `core/include/core/model/TransformerModel.h`
ligne 145-152 a déjà `directDynLosses_.computeFieldSeparated(...)`, mais elle
est **désactivée en mode Physical** :

```cpp
if (directDynLosses_.isEnabled()
    && config_.calibrationMode != CalibrationMode::Physical) {  // ← ici
    const auto fsep = directDynLosses_.computeFieldSeparated(...);
    ...
```

**Recommandation Sprint 3 :**

1. **Avant** d'implémenter le NR couplé, **enlever la garde
   `!= CalibrationMode::Physical`** et activer `computeFieldSeparated` en
   Physical aussi.
2. Vérifier que la branche field-separation gère correctement le bas H
   (probablement déjà OK puisqu'elle est éprouvée en mode Artistic).
3. Si la pathologie sign-flip persiste : alors seulement passer au NR
   couplé.

Ça transforme Sprint 3 d'une refonte de solveur en une **modification de 1 ligne**
+ tests de non-régression. Le sprint plan disait "Si tu trouves un blocker, tu le
documentes et tu reviens vers l'utilisateur" — ici le blocker théorique est que
l'audit propose la solution Baghel a déjà invalidée.

### 3.4 Si on garde le forward-NR couplé (plan B)

Jacobien analytique propre :

```
g(H)       = R_JA(H_eff)
H_eff(H)   = H − K1·u − K2·σ·√|u|       avec σ = sign(u), u = (B_JA(H) − B_committed)/Ts

dg/dH      = (∂R_JA/∂H_eff) · [1 − K1·(du/dH) − (K2/(2√|u|))·(du/dH)]
du/dH      = (1/Ts) · (dB_JA/dH)         ← susceptibilité différentielle J-A
```

**Singularité u → 0** : trois traitements connus, par ordre de simplicité :

1. **Soft-floor** : `√|u| → √(|u| + ε²)`, ε ≈ 1e-3·B_sat/Ts. Régularise C¹,
   convergence Newton préservée. *Bottauscio & Chiampi 2002, Ducharne 2018.*
2. **Cap Jacobien** : si `|u| < u_min`, force `K2/(2√u_min)`. Plus brutal.
3. **Sub-step adaptatif** : si `|u| < seuil`, désactive Bertotti pour ce
   sample (justifié physiquement: pas de courants induits).

**Convergence + fallbacks** :
- Critère 1 : ‖g(H_k)‖ ne décroît pas sur 2 iter consécutives (Armijo).
- Critère 2 : |H_dyn| / |H_stat| > 0.5 — Bertotti domine, retour bisection.
- Critère 3 : sign(H_eff_k) ≠ sign(H_eff_{k-1}) — oscillation.
- Hiérarchie : Newton damped → bisection 30 iter → bypass Bertotti.

`maxOuterIter=8` est confortable à 176.4 kHz (procRate Physical ×4) **sauf**
au zero-crossing dB/dt + bas H ; logger en debug.

### 3.5 Refs additionnelles 2020-2024 (DOIs à reverifier)

- Steentjes, Hameyer *et al.*, "Dynamic energy-based J-A including excess
  loss", IEEE TMag 56(1) 2020, DOI 10.1109/TMAG.2019.2952272 — formulation
  énergétique, évite la singularité √|·|.
- Quondam Antonio *et al.*, "Identification + updating of an inverse J-A for
  dynamic hysteresis", *JMMM* 545 (2022).
- Hamel *et al.*, "Improved NR for dynamic J-A with sigmoidal damping",
  *COMPEL* 41(4) 2022.
- Padilha *et al.*, "Vector hysteresis for real-time power electronics",
  *IEEE OJ-PEL* 5 (2024) — bench J-A dyn vs Preisach dyn vs viscous.

---

## Sprint 4 — Polyphase halfband oversampling

### 4.1 Le code actuel a deux bugs structurels

**Bug 1 — N=53 ne respecte pas 4k+3.** Pour qu'un Kaiser-windowed sinc soit
strictement halfband (h[2k]=0 pour k≠0, seul h[0] = ½), il faut **N ≡ 3
(mod 4)**, donc M = (N-1)/2 impair. 53 = 4·13+1 → M=26 pair → propriété
halfband **cassée**, certains taps pairs ne sont pas exactement nuls.

**Bug 2 — pas de polyphase Noble identity.** Le code fait
`filter.process(input)` ET `filter.process(0.0f)` au double rate (zero-stuff
puis filtrer tout) → **8× plus de calculs que nécessaire**.

### 4.2 Design Kaiser pour cible 100 dB stopband

Formules Kaiser 1974 / Oppenheim-Schafer eq. 7.75-7.76 :

```
β = 0.1102·(A − 8.7)              (A > 50 dB)
N ≈ ⌈(A − 8) / (2.285·Δω)⌉ + 1     Δω = 2π·Δf, Δf = 0.5 − 2·f_p
N → arrondi au prochain 4k+3      (halfband strict)
```

| Cible | f_p (cycles/sample) | Δf | β | **N (4k+3)** | Group delay M |
|---|---|---|---|---|---|
| 80 dB legacy (réf actuel) | 0.20 | 0.10 | 7.857 | **51 ou 55** (audit casse) | 25 ou 27 |
| **100 dB cible audit** | 0.21 | 0.08 | **10.061** | **83** | **41** |
| 100 dB HQ (transition serrée) | 0.225 | 0.05 | 10.061 | **131** | 65 |
| 120 dB | 0.21 | 0.08 | 12.265 | **103** | 51 |

**Recommandation par stage Sprint 4 :** **N = 83, β = 10.06, f_p = 0.21**.

### 4.3 Latence cascade 2x→2x

Group delay FIR linéaire-phase = (N-1)/2 samples au rate du filtre. Cascade
2x→2x = 4x total :

```
latence (samples original-rate) = GD₁ + GD₂/2
```

Avec GD₁ = GD₂ = 41 (deux stages 100 dB identiques) : **41 + 20.5 = 61.5
samples**. Le sprint plan annonce ~63, cohérent.

JUCE 6+ accepte float dans `setLatencySamples()`, sinon arrondir à 62.

### 4.4 Économie de calcul

- Naïf actuel : ~166 MACs / sample @ 2fs (filtre full au double rate).
- Polyphase halfband : **~22 MACs / sample @ fs original-rate** par direction,
  total cascade up+down ≈ **180 MACs / sample original** (< 5 % CPU sur i5
  moderne à 96 kHz).
- Speedup ~8× grâce à E0 = pure delay (½·z⁻ᴹ, 1 tap centré) + symétrie
  linéaire-phase E1 (21 taps uniques sur 41).

### 4.5 Implémentation C++ recommandée

```cpp
class HalfbandPolyphase {
    static constexpr int N = 83;          // 4k+3
    static constexpr int M = (N-1)/2;     // 41 (impair)
    static constexpr int kHalfE1 = (M+1)/2; // 21 unique odd taps (symétrie linéaire-phase)
    alignas(32) std::array<float, kHalfE1> e1Coeffs_;  // h[1], h[3], ..., h[M]
    alignas(32) std::array<float, M+1>     delayE1_{};
    std::array<float, M+1>                 delayE0_{};
    int posE1_ = 0, posE0_ = 0;
};

// Upsample: produit 2 samples @ 2fs pour 1 input @ fs (pas de zero-stuff)
output[2k]   = 0.5f * delayE0[k - M/2];
output[2k+1] = computeE1(input[k]);

// Downsample: consomme 2 samples @ 2fs, produit 1 @ fs (pas de discard)
y = 0.5f * x_even[n - M/2] + dotE1(x_odd, e1Coeffs);
```

**SIMD AVX2** : dot-product de 21 taps via `_mm256_fmadd_ps`, 3 itérations
FMA + reduce horizontal. Pré-calculer paires symétriques `x[k] + x[N-1-k]`
pour halver les multiplications.

### 4.6 Garanties strictes : Parks-McClellan > Kaiser

**Pièges Kaiser** :
- Le sinc Kaiser-windowed *par calcul flottant* peut produire des taps pairs
  à 1e-10 résiduels, pas exactement zéro → légère fuite stopband en cascade.
- Solution la plus propre : **Parks-McClellan halfband équiripple** (algo
  Vaidyanathan §4.6.2 ; `firhalfband` MATLAB ; `scipy.signal.remez` détourné
  ou bibliothèque dédiée). PM **construit la propriété par design** — taps
  pairs garantis bit-exact zéro.
- Si on garde Kaiser pour simplicité : **forcer explicitement les taps pairs
  à zéro** après calcul (sauf le central = ½).

### 4.7 Cascade stopband — attention

Cascade 2x→2x ne donne **pas** A1 + A2 dB. La pire région d'aliasing est
souvent dominée par **min(A1, A2)** (les images de stage 1 entre 0.5fs et
1.5fs sont rejetées par stage 2 si stage 2 a transition serrée). **Designer
chaque stage à 100 dB pour garantir 100 dB total** ; vérifier en simulation
FFT (test acceptance criterion sprint plan).

### 4.8 Option future : IIR polyphase Regalia-Mitra

Pour un mode "low latency" (sprint plan §4 INTERDIT le mentionne comme
option) :
- Décomposition halfband en somme de deux all-pass : H(z) = ½·[A0(z²) +
  z⁻¹·A1(z²)].
- Coefficients = pôles réels via design elliptique (Schüssler 1980,
  Lutovac 2001).
- Coût : 4-12 multiplies pour 100 dB.
- Phase non-linéaire (saturation tolère).
- Latence ~5 samples vs 62 samples FIR équivalent.

**Référence implémentation** : **HIIR de Laurent de Soras**
(`ldesoras.free.fr/prod.html#src_hiir`, WTFPL). Cascade 2x configurable
jusqu'à 156 dB. JUCE `juce::dsp::Oversampling` propose les deux modes
(`filterHalfBandFIREquiripple`, `filterHalfBandPolyphaseIIR`) — exactement
la dichotomie discutée.

### 4.9 Refs Sprint 4

1. **Vaidyanathan 1993** *Multirate Systems and Filter Banks* — §4.2 (Noble
   identities, p. 104-110), §4.3 (polyphase, p. 119-127), §4.6 (halfband,
   p. 140-149). Référence canonique.
2. **Crochiere & Rabiner 1983** *Multirate DSP*. Ch. 3-4.
3. **Oppenheim & Schafer 2010** *DTSP* 3e éd. §7.6 Kaiser eq. 7.75-7.76.
4. **Smith JOS** *Spectral Audio Signal Processing*, ccrma.stanford.edu/~jos/sasp/.
5. **Regalia, Mitra, Vaidyanathan 1988** "The digital all-pass filter…"
   *Proc. IEEE* 76(1):19-37. IIR polyphase base.
6. **HIIR (Laurent de Soras)** ldesoras.free.fr/prod.html#src_hiir —
   référence pratique open-source.
7. **r8brain-free-src (Vaneev)** github.com/avaneev/r8brain-free-src.
8. **JUCE source** `modules/juce_dsp/processors/juce_Oversampling.cpp` —
   coefficients pré-calculés A=70/100/120 dB.

---

## Synthèse — impact sur les sprints

| Sprint | Décision attendue | Risque |
|---|---|---|
| **Sprint 2 hScale** | OK, déjà implémenté en `CalibrationMode::Physical`. Activer par défaut sur Jensen presets. | Faible. |
| **Sprint 2 bNorm** | ⚠️ **Investiguer la cause du +4 dB AVANT de toucher bNorm**. La théorie dit que le ratio χ_minor/χ_eff est < 1 (−3.3 dB), pas > 1 (+4 dB). | Possibilité que le fix audit aggrave la situation au lieu de l'arranger. Test diagnostic dédié à écrire d'abord. |
| **Sprint 3 Bertotti** | ⚠️ **Tenter d'abord d'enlever la garde `!= Physical` sur `computeFieldSeparated`** plutôt que d'implémenter le NR couplé. Le NR couplé proposé par l'audit est exactement la "original JA" que Baghel 2014 a démontrée instable. | Si la simple ré-activation suffit, Sprint 3 passe d'1 sprint à ~1 jour. Sinon, plan B = NR couplé avec Jacobien §3.4. |
| **Sprint 4 polyphase** | OK, design théorique solide. **N=83, β=10.06, f_p=0.21**, latence ~62 samples. Préférer Parks-McClellan à Kaiser pour garantie tap-zero stricte. | Faible — calcul à confirmer numériquement avec scipy avant de figer les coefficients. |
| **Sprint 5** | Pas de gap théorique. | — |

**Action recommandée avant Sprint 2 :**

1. Écrire un test diagnostic isolé qui mesure la susceptibilité différentielle
   J-A à M=0 en régime établi (pas en transient virgin-state) sur le preset
   JT-115K-E.
2. Comparer la valeur mesurée avec χ_eff actuel ET avec χ_minor =
   c·χ₀/(1−α·c·χ₀).
3. Selon le résultat, ajuster le plan Sprint 2 (garder χ_eff, passer à
   χ_minor, ou identifier une 3e cause au +4 dB).

**Action recommandée avant Sprint 3 :**

1. Patcher temporairement `TransformerModel.h` pour enlever la garde
   `!= CalibrationMode::Physical` sur `computeFieldSeparated`.
2. Re-faire le baseline mesure (relancer `validate_jensen` Physical).
3. Vérifier si la pathologie sign-flip + le wedge à 1.15 % à 20 Hz
   disparaissent.
4. Si oui : Sprint 3 réduit à un commit + tests. Si non : Sprint 3 plan B
   (forward NR couplé avec Jacobien §3.4).

---

*Compilé 2026-04-26 par 3 agents de recherche parallèles (sandbox sans
WebSearch/WebFetch). Les équations sont vérifiables ; les DOIs récents
(2018-2024) restent à reconfirmer en ligne avant publication finale.*
