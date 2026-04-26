# Prompt GPT-5.4 Thinking — TWISTERION Phase 3 : 2 problemes restants

## Contexte global

Plugin audio VST3/AAX modelisant un channel strip analogique (transformateur d'entree T1 + preampli + transformateur de sortie T2) en temps reel. Architecture:
- **Wave Digital Filters (WDF)** : CRTP one-port pour BJT, diodes, transformateurs
- **Jiles-Atherton hysteresis** : Implicit NR avec integration trapezoidale, Langevin-Pade [3/3]
- **Newton solver** : Boucle delay-free feedback `g(y) = y - F(x, beta*y) = 0` avec Jacobien analytique
- **Dual topology** : Path 0 = Neve Class-A (Q1 CE → Q2 CE → Q3 EF), Path 1 = JE-990 discrete op-amp (DiffPair → Cascode → VAS → ClassAB push-pull → LoadIsolator)
- **C++17 header-only**, sample rate variable (44.1k-192k), temps reel strict

## Ce qui a ete fait dans cette session

### Phase 3 UAD Critique (3 Sprints)
1. **Sprint A** : VAS double-degen diagnostique confirme correct (gain 338.2 = theorie 338.5). T2 load change 600Ω→10kΩ (bridging standard). VAS tanh soft saturation remplace clamp ±200V.
2. **Sprint B** : ClassAB Ic-weighted crossover (H2/H3=0.018 isole). getLocalGain() signal-dependent (0.819–0.980). Hybrid Newton (g0 analytique, commit nonlineaire). Level match Heritage-Modern: 1.31 dB.
3. **Sprint C** : Tests THD low-level, T2Load plugin parameter, KNOWN_LIMITATIONS.md.

### Corrections post-consultation GPT (3 fixes paralleles)
1. **P1 — VAS even-harmonic suppression** : Correction analytique H2 dans VASStageWDF.h. Estime composante paire via terme quadratique BJT `(gm/2Vt)*vbe²`, supprime avec facteur η dependant du loop gain instantane. → H2 reduit mais PAS assez pour inverser le ratio H2/H3 en full chain.
2. **P2 — Jensen NR convergence** : Line search monotone backtracking (4 halvings), warm-start slope-aware en deep saturation, bisection reduite 20→8 iter. → **11/11 PASS, max 16 iter (etait 28). RESOLU.**
3. **P3 — Bilinear prewarping** : Fonction `prewarpHz(f, fs)` ajoutee dans Constants.h. Appliquee a 18 sites de calcul de coefficients BLT dans 11 fichiers (Miller LP, feedback HP, servo, DC tracking, lead comp, load isolator, transformer HP). WDF port impedances NON modifiees (correctes par construction). → **Amelioration partielle seulement.**

### Regression actuelle : 30/32 PASS (objectif 32/32)
- `jensen_convergence` : ✅ RESOLU
- `odt_differentiation` : ❌ 16/18 (2 echecs H2/H3)
- `sample_rate_invariance` : ❌ 2/10 (8 echecs)

---

## PROBLEME 1 : H2 > H3 dans le JE-990 full chain (odt_differentiation)

### Symptome
```
Modern: H1=-41.31 dB  H2=-70.05 dB  H3=-79.11 dB  H2/H3=9.06 dB  THD=3.94%
```
Le test attend H3 > H2 (signature push-pull). En isole, le ClassAB donne bien H3 >> H2 (ratio 0.018). Mais dans la chaine complete, le VAS single-ended injecte du H2 qui domine.

### Ce qui a ete tente
Correction analytique dans VASStageWDF.h processSample():
```cpp
// Apres Vc_ac = -Ic_ac * R_coll_AC (ligne 182)
{
    const float gm = bjtLeaf_.getGm();
    const float Vt = config_.bjt.Vt;
    lastInputDC_ += 0.001f * (input - lastInputDC_);
    const float vbe_ac = input - lastInputDC_;
    if (gm > kEpsilonF && std::abs(vbe_ac) > kEpsilonF)
    {
        const float Ic_even = (gm / (2.0f * Vt)) * vbe_ac * vbe_ac;
        constexpr float kEvenSuppK = 0.6f;
        constexpr float kEvenFloor = 0.15f;
        constexpr float kEvenCeil  = 1.0f;
        const float loopGainEst = std::abs(Vc_ac) / (std::abs(vbe_ac) + 1e-10f);
        float eta = 1.0f / (1.0f + kEvenSuppK * std::min(loopGainEst, 100.0f));
        eta = std::clamp(eta, kEvenFloor, kEvenCeil);
        Vc_ac += (1.0f - eta) * Ic_even * config_.R_coll_AC;
    }
}
// Puis tanh soft saturation, degeneration, Miller LP, DC offset removal
```

### Pourquoi ca n'a pas suffi
- η clamp a [0.15, 1.0] signifie max 85% de suppression de la composante paire
- Le loop gain instantane `|Vc_ac/vbe_ac|` est tres eleve (~338), donc η ≈ 0.15 (floor)
- Mais H2 reste 9 dB au-dessus de H3, ce qui suggere que la correction est trop faible OU que le modele de la composante paire est imprecis
- Possible que la tanh soft saturation (ligne 236) re-genere du H2 apres la correction
- Le terme quadratique `(gm/2Vt)*vbe²` sous-estime peut-etre la contribution des termes d'ordre superieur pairs (H4, H6...)

### Architecture et contraintes
- Le VAS est evalue 2 fois par sample (probe g0 + commit) dans le Newton loop de JE990Path
- L'etat du VAS est snapshot/restore via AcSnap struct (lastInputDC_ inclus)
- La correction doit etre **lisse et derivable** pour ne pas casser le Jacobien
- Le ClassAB isole donne correctement H3 >> H2 (H2/H3 = 0.018)
- Le loop gain WDF est ~60 dB vs ~125 dB reel

### Structure de la chaine JE-990 (processSample dans JE990Path.h)
```
1. Snapshot DiffPair, Cascode, VAS, ClassAB
2. Jacobien analytique: J = 1 + beta * Av_dp * Av_cas * Av_vas * Av_ab
3. Probe g0: DiffPair→Cascode→VAS en full, ClassAB via getLocalGain() (analytique)
4. Newton step: y = y - g0/J
5. Commit: DiffPair→Cascode→VAS→ClassAB.processSample() (nonlineaire)
6. DC servo, Load isolator, C_out HP
```

### Questions specifiques
1. La tanh(Vc_ac/Vc_clip) APRES la correction even-harmonic re-genere-t-elle du H2 ? Si oui, faut-il integrer la correction DANS la loi de soft clip plutot qu'avant ?
2. Le terme `(gm/2Vt)*vbe²` est-il le bon estimateur ? Le BJTLeaf utilise le modele compagnon Ebers-Moll complet — faudrait-il extraire la composante paire directement de `Ic - (Ic_q + gm*vbe)` au lieu de la recalculer ?
3. La suppression de H2 au niveau du VAS est-elle la bonne approche, ou devrait-on plutot augmenter le loop gain effectif pour le H2 specifiquement (filtre selectif dans la boucle feedback) ?
4. Le kEvenSuppK=0.6 et kEvenFloor=0.15 sont-ils bien calibres ? Faudrait-il kEvenFloor=0.0 (suppression totale possible) ?

### Donnees numeriques
- VAS standalone: gain = 338.2, Ic_q = 1.5 mA, gm ≈ 58 mA/V, Vt = 26 mV
- R_coll_AC = 60 kΩ, R_emitter = 160Ω, C_miller = 150 pF
- ClassAB isole: H2/H3 = 0.018 (H3 >> H2, correct)
- Full chain: H2 = -70.05 dB, H3 = -79.11 dB (H2 9 dB au-dessus)
- beta feedback = Rg/(Rfb+Rg) = 47/(1430+47) ≈ 0.032
- Loop gain: Aol * beta ≈ 338 * 0.98 * 6.3 * 0.95 * 0.032 ≈ 63 (36 dB)

---

## PROBLEME 2 : Invariance sample rate 44.1k vs 96k (sample_rate_invariance)

### Symptome
```
=== Neve path: 44.1k vs 96k ===
  44.1 kHz: RMS=0.007132  mag=-41.6 dB  THD=19.873%
  96.0 kHz: RMS=0.006577  mag=-43.6 dB  THD=33.117%
  Magnitude ratio: -2.02 dB (seuil: ±1 dB) ❌
  THD ratio: 1.666 (seuil: 0.5–1.5) ❌

=== JE-990 path: 44.1k vs 96k ===
  44.1 kHz: mag=-41.3 dB  THD=3.934%
  96.0 kHz: mag=-41.9 dB  THD=0.269%
  Magnitude ratio: -0.59 dB ✅
  THD ratio: 0.068 (seuil: 0.5–1.5) ❌

=== Frequency response 44.1k vs 96k ===
  Neve 50 Hz:   diff=-11.19 dB ❌ (seuil ±2 dB)
  Neve 1 kHz:   diff=-2.02 dB  ❌ (borderline)
  Neve 10 kHz:  diff=-2.49 dB  ❌
  JE990 50 Hz:  diff=-11.38 dB ❌
  JE990 1 kHz:  diff=-0.59 dB  ✅
  JE990 10 kHz: diff=+15.41 dB ❌
```

### Ce qui a ete fait
- `prewarpHz(f, fs) = (fs/pi) * tan(pi*f/fs)` applique a tous les filtres BLT (18 sites, 11 fichiers)
- Filtres concernes: Miller LP, feedback HP, DC servo, DC tracking, lead comp, load isolator, T1/T2 HP, transformer Lm-based HP
- WDF port impedances (Z=2L/T, Z=T/2C) NON modifiees car correctes pour le framework WDF trapezoidal

### Pourquoi ca n'a PAS suffi

**Hypothese 1 — Les WDF reactive elements sont le vrai probleme:**
Les capacites et inductances WDF utilisent la discretisation trapezoidale standard:
```cpp
Z_port_inductor = 2 * L / Ts;    // ∝ sample_rate
Z_port_capacitor = Ts / (2 * C);  // ∝ 1/sample_rate
```
Quand le sample rate double, toutes les impedances de port changent. La topologie WDF (adapteurs paralleles/series + non-linearites) compense partiellement, mais le mapping frequence analogique→numerique est inherent au BLT:
```
s → (2/Ts) * (z-1)/(z+1)
```
A 44.1 kHz, les frequences >15 kHz sont comprimees vers Nyquist. A 96 kHz, cette compression commence seulement a >30 kHz. **Ceci est fondamental et non corrigeable par prewarping des seuls filtres BLT externes.**

**Hypothese 2 — Le modele Jiles-Atherton change de comportement avec Ts:**
L'integration trapezoidale du J-A:
```
M_new - M_old = 0.5 * (dM/dH_new + dM/dH_old) * dH
```
Quand Ts diminue (96 kHz), dH par sample diminue, ce qui modifie la trajectoire dans la boucle d'hysteresis. L'effet est non-lineaire: la permeabilite effective et le seuil de saturation changent avec le pas temporel.

**Hypothese 3 — Le JE990 10kHz +15 dB pointe vers le VAS Miller:**
Le pole Miller est a fc ≈ 17.7 kHz. A 44.1 kHz (Nyquist=22.05 kHz), le pole est a 0.80 de Nyquist — tres deforme par le BLT. A 96 kHz (Nyquist=48 kHz), le pole est a 0.37 de Nyquist — bien place. Le prewarping ajuste la frequence de coupure mais pas la forme de la reponse proche de Nyquist. Le gain VAS a 10 kHz est donc tres different entre les deux rates.

**Observation critique — Le 50 Hz -11 dB est identique sur les deux paths:**
Neve et JE-990 partagent T1 (InputStage) et T2 (OutputStage). L'ecart a 50 Hz est probablement dans les transformateurs, pas dans l'ampli. La HP du transformateur depend de Lm (magnetizing inductance), qui est modulee par l'hysteresis J-A. Si le J-A se comporte differemment a 96 kHz, le Lm effectif change, decalant le HP.

### Architecture des transformateurs
```
T1 (InputStage.h):
  - TransformerCircuitWDF: L_leakage, C_pri_shield, C_sec_reflected (WDF tree)
  - HP filter: hpAlpha = omega_w / (1 + omega_w), avec fc = Zs_eff / (2pi*Lp)
  - Prewarp applique ✅

T2 (OutputStage.h):
  - TransformerCircuitWDF: meme structure
  - HP filter: identique a T1
  - Prewarp applique ✅

TransformerModel.h (hysteresis):
  - computeHpAlphaFromLm(Lm): fc = Rsource / (2pi*Lm), prewarp ✅
  - Jiles-Atherton: implicit NR, integration trapezoidale
  - hpAlphaMin_: prewarped ✅
  - lmSmoothCoeff_: prewarped ✅
  - Legacy LP/HP fallbacks: prewarped ✅
```

### Donnees de la chaine de signal (prepareToPlay)
```
PreampModel.prepareToPlay(sampleRate, maxBlockSize):
  1. inputStage_.prepare(sampleRate, config_.input)  // T1
  2. nevePath_.prepare(sampleRate, maxBlockSize)      // Neve amplifier
  3. je990Path_.prepare(sampleRate, maxBlockSize)     // JE-990 amplifier
  4. outputStage_.prepare(sampleRate, config_.t2Config) // T2
```
Chaque etage recoit le sample rate et recalcule ses coefficients. Les WDF port impedances sont recalculees automatiquement par le framework.

### Questions specifiques
1. Les WDF port impedances (2L/T, T/2C) sont-elles la cause principale de la variance, et si oui, peut-on les compenser par un ajustement des valeurs de composants analogiques (`L_warped = L * tan(pi*fc/fs) / (pi*fc/fs)`) cible sur les frequences critiques ?
2. Le modele J-A devrait-il tourner a un sample rate interne fixe (ex: 4×44.1k) avec resampling, pour eliminer la dependance au Ts du host ? Quel cout CPU en plus ?
3. Le pole Miller a 17.7 kHz pose-t-il un probleme fondamental a 44.1 kHz (0.80 Nyquist) que le prewarping seul ne peut pas resoudre ? Faudrait-il un filtre TPT (topology-preserving transform) au lieu du one-pole BLT ?
4. Le -11 dB a 50 Hz identique sur les deux paths pointe vers T1/T2. Peut-on confirmer en bypassant les transformateurs et en mesurant la variance de l'ampli seul ?
5. Si l'oversampling interne est necessaire, quelle architecture (par-stage vs global, facteur 2× vs 4×) minimise le CPU tout en resolvant le probleme ?

### Contraintes temps reel
- Budget CPU: <5% d'un coeur a 96 kHz, buffer 512 samples
- Pas de FFT dans le chemin audio (only Goertzel dans les tests)
- Le J-A solver actuel fait 3-4 iterations NR en regime normal, max 16 en deep saturation
- L'oversampling global 2× doublerait le CPU (~10% a 96k)
- L'oversampling du seul bloc transfo serait plus economique

---

## Resume des metriques a atteindre

| Test | Metrique | Seuil | Actuel | Ecart |
|------|----------|-------|--------|-------|
| odt H3>H2 | H3/H2 ratio | > 1.0 | 0.352 (H2 dominant) | ×2.84 |
| odt H2/H3 | H2/H3 < 1.0 | < 1.0 | 2.838 | ×2.84 |
| SR mag Neve | 96k/44k ratio | ±1 dB | -2.02 dB | 1.02 dB |
| SR THD Neve | 96k/44k ratio | 0.5–1.5 | 1.666 | 0.166 |
| SR THD JE990 | 96k/44k ratio | 0.5–1.5 | 0.068 | 0.432 |
| SR 50Hz Neve | diff | ±2 dB | -11.19 dB | 9.19 dB |
| SR 50Hz JE990 | diff | ±2 dB | -11.38 dB | 9.38 dB |
| SR 1kHz Neve | diff | ±2 dB | -2.02 dB | 0.02 dB |
| SR 10kHz Neve | diff | ±2 dB | -2.49 dB | 0.49 dB |
| SR 10kHz JE990 | diff | ±2 dB | +15.41 dB | 13.41 dB |

## Format de reponse attendu

Pour chaque probleme:
1. **Diagnostic de la cause racine** avec explication physique/numerique precise
2. **Solution concrete** avec pseudo-code C++ insertable dans l'architecture existante
3. **Risques et effets de bord** de la solution proposee
4. **Metriques de validation** pour confirmer que le fix fonctionne
5. **Ordre d'implementation** si plusieurs changements sont necessaires
