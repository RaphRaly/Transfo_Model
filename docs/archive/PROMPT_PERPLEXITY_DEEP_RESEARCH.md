Recherche deux problemes non resolus dans un plugin audio de modelisation analogique temps reel (WDF + Jiles-Atherton). J'ai besoin de solutions fondees sur la litterature academique DSP/WDF et les pratiques etablies de l'industrie audio pro (UAD, Arturia, Analog Obsession, etc). Cherche dans les publications IEEE, DAFx, AES, SMAC, les theses de Kurt James Werner, Julius O. Smith, Stefano D'Angelo, Maarten van Walstijn, et les ressources de sound-au.com, dsprelated.com, KVR developer forum.

---

# Contexte du projet

Plugin VST3/AAX C++17 header-only modelisant un channel strip analogique complet :
- **Transformateur d'entree T1** (Jensen JT-115K-E) → **Preampli dual topology** → **Transformateur de sortie T2** (Jensen JT-11ELCF)
- **Path 0 = Neve 1073 Class-A** : Q1 BC184C CE → Q2 BC214C CE → Q3 BD139 EF, feedback avec C6 470uF
- **Path 1 = Jensen JE-990 discrete op-amp** : DiffPair LM-394 → Cascode → VAS Q6 2N4250A PNP → ClassAB Q8 MJE-181 + Q9 MJE-171 push-pull → LoadIsolator 39Ω+40uH
- **Framework WDF** avec adapteurs serie/parallele, CRTP one-port BJT/diode leaves (Ebers-Moll companion source, Newton-Raphson implicite 8 iter max)
- **Hysteresis Jiles-Atherton** : NR implicite, integration trapezoidale, Langevin Pade [3/3], damped line search avec bisection fallback
- **Newton delay-free feedback loop** dans JE990Path : `g(y) = y - F(x, beta*y) = 0`, Jacobien analytique `J = 1 + beta * Av_dp * Av_cas * Av_vas * Av_ab`, 1 probe + 1 commit par sample
- Sample rate variable 44.1k–192k, budget CPU <5% d'un coeur a 96k, buffer 512

Regression actuelle : 30/32 tests PASS. Les 2 echecs restants sont decrits ci-dessous.

---

# PROBLEME 1 — H2 domine H3 dans le JE-990 full chain

## Le symptome mesure
```
JE-990 full chain, 1kHz sine, gain position 5 (29.9 dB):
  H1 = -41.31 dB
  H2 = -70.05 dB
  H3 = -79.11 dB
  H2/H3 = 9.06 dB (H2 dominant)
  THD = 3.94%
```
Le test attend H3 > H2 (signature push-pull odd-harmonic). En isolation, le ClassAB WDF produit correctement H3 >> H2 (ratio H2/H3 = 0.018). Mais dans la chaine complete, le VAS single-ended common-emitter (Q6 PNP) injecte du H2 qui masque le H3 du ClassAB.

## Ce que j'ai deja tente
Correction analytique dans VAS processSample(), inseree AVANT la tanh soft saturation :
- Estime la composante paire via le terme quadratique BJT : `Ic_even = (gm/2Vt) * vbe_ac^2`
- Supprime avec facteur `eta = 1/(1 + 0.6 * loopGainEst)`, clampe a [0.15, 1.0]
- Ajoute `(1-eta) * Ic_even * R_coll_AC` a Vc_ac pour compenser le biais negatif

Resultat : H2 legerement reduit, mais encore 9 dB au-dessus de H3. La correction au floor (eta=0.15, 85% suppression) ne suffit pas.

## Les parametres physiques
- VAS : gm ≈ 58 mA/V, Vt = 26 mV, Ic_q = 1.5 mA, R_coll_AC = 60 kOhm, R_emitter = 160 Ohm
- VAS AC gain = 338 (verified standalone)
- Soft saturation : `Vc_clip * tanh(Vc_ac / Vc_clip)` ou Vc_clip = 0.9 * Vcc = 21.6V
- ClassAB : Ic-weighted crossover `f8 = |Ic_q8| / (|Ic_q8| + |Ic_q9|)`, getLocalGain() = gm_total / (gm_total + g_sense)
- Feedback : beta = 47/(1430+47) = 0.032, loop gain ≈ 36 dB (WDF) vs 125 dB (reel)
- La tanh soft clip est une fonction IMPAIRE — elle ne devrait pas generer de H2 directement. Mais appliquee a un signal qui a deja un DC offset (meme infime du au H2 du BJT), elle pourrait amplifier l'asymetrie.

## Ce que je cherche
1. **Dans la litterature WDF** (Werner 2016 thesis, D'Angelo 2014, Fettweis 1986) : comment les implementations WDF d'amplificateurs a feedback gerent-elles la dominance H2 d'un etage CE single-ended ? Y a-t-il un precedent de correction de symetrie dans le domaine onde ?
2. **Dans l'industrie** : comment UAD, Arturia, Plugin Alliance modelisent-ils la signature harmonique push-pull d'un ampli op discret (API 2520, JE-990, Fred Forssell) quand le loop gain WDF est inferieur au reel ? Utilisent-ils un gain de boucle artificiel, une compensation post-hoc, ou une autre approche ?
3. **Analyse mathematique** : si la correction `(gm/2Vt)*vbe^2` ne suffit pas, est-ce parce que les termes d'ordres superieurs (H4, H6 via `vbe^4`, `vbe^6`) contribuent aussi ? Faudrait-il extraire la composante paire directement comme `Ic_even = Ic_ac - gm*vbe_ac` (difference entre le courant reel et l'approximation lineaire) ?
4. **Approche alternative** : plutot que de supprimer H2 au VAS, devrait-on appliquer un filtre de boucle feedback qui augmente selectivement le loop gain aux frequences paires (2f0, 4f0) ? Ou integrer la correction dans la loi de soft clip (tanh anti-symetrique corrigee) ?
5. **Calibration** : quelles metriques H2/H3 mesure-t-on sur un vrai JE-990 (datasheet Jensen, papiers AES) pour calibrer la correction ? Le ratio H3/H2 > 1.0 est-il meme realiste en full chain, ou le vrai circuit a-t-il aussi un peu de H2 residuel ?

---

# PROBLEME 2 — Variance sample rate 44.1k vs 96k

## Les symptomes mesures
```
=== Magnitude 1 kHz (seuil: ±1 dB) ===
  Neve:  -2.02 dB (96k plus faible)     ❌
  JE990: -0.59 dB                        ✅

=== THD ratio 96k/44k (seuil: 0.5–1.5) ===
  Neve:  1.666 (THD augmente a 96k)      ❌
  JE990: 0.068 (THD quasi-disparait)     ❌

=== Frequency response diff 96k-44k (seuil: ±2 dB) ===
  Neve 50 Hz:   -11.19 dB               ❌
  Neve 1 kHz:   -2.02 dB                ❌ (borderline)
  Neve 10 kHz:  -2.49 dB                ❌
  JE990 50 Hz:  -11.38 dB               ❌
  JE990 1 kHz:  -0.59 dB                ✅
  JE990 10 kHz: +15.41 dB               ❌
```

## Ce qui a deja ete fait
Bilinear prewarping `fc_w = (fs/pi)*tan(pi*fc/fs)` applique a TOUS les filtres BLT one-pole (18 sites dans 11 fichiers) : Miller LP, feedback HP, DC servo, DC tracking, lead comp, load isolator, T1 HP, T2 HP, transformer Lm-based HP, etc. WDF port impedances (Z=2L/T, Z=T/2C) NON modifiees car la theorie WDF dit qu'elles sont correctes pour la discretisation trapezoidale.

Resultat : quasi aucune amelioration.

## Observations cles pour le diagnostic
1. **50 Hz -11 dB identique sur Neve ET JE-990** → le probleme est dans T1/T2 (partages), pas dans les amplis
2. **JE990 10 kHz +15.41 dB** → a 44.1k, le signal a 10kHz est fortement attenue ; a 96k, il passe. Pointe vers le pole Miller VAS (17.7 kHz) ou la bande passante du Newton solver qui depende du nombre de samples par cycle
3. **THD JE990 divise par 58× a 96k** → le comportement non-lineaire du JE-990 change radicalement avec le pas temporel. Le Newton solver fait 1 iteration par sample ; avec 2× plus de samples par cycle, la boucle converge mieux et linearise le circuit
4. **THD Neve augmente de 67% a 96k** → le Neve n'a PAS de Newton loop, juste des stages en cascade. L'augmentation de THD pointe vers un changement de point de fonctionnement (DC) lie aux transformateurs

## Architecture des transformateurs
```
T1 (InputStage): TransformerCircuitWDF tree
  - WDF elements: L_leakage (5 mH), C_pri_shield (475 pF), C_sec_reflected
  - Port impedances: Z_L = 2*L/Ts, Z_C = Ts/(2*C) — recalculees a chaque prepare()
  - HP filter post-WDF: hpAlpha prewarped ✅
  - Jiles-Atherton: implicit NR trapezoidal, dynamic Lm modulation

T2 (OutputStage): meme structure WDF
  - Source impedance dynamique (power-weighted crossfade Neve/JE990)
  - HP filter prewarped ✅
```

## Les 3 hypotheses a investiguer

### H1: Le modele Jiles-Atherton est Ts-dependant
L'integration trapezoidale `M_new = M_old + 0.5*(dM/dH_new + dM/dH_old)*dH` avec dH qui diminue quand Ts diminue change la trajectoire dans la boucle B-H. A 96 kHz, les pas sont 2× plus petits, ce qui pourrait :
- Modifier la permeabilite effective μ_eff et donc le Lm dynamique
- Changer le seuil de saturation effectif
- Decaler la frequence de coupure HP du transformateur (fc = R_source / (2pi*Lm))

**Cherche** : Y a-t-il des publications sur la dependance au pas temporel du modele Jiles-Atherton discret ? Quelles solutions existent (normalisation, oversampling interne du modele J-A, methode de Chua) ?

### H2: Les WDF port impedances changent le transfer function global
Meme si chaque element WDF est "correct" pour la discretisation trapezoidale, le transfer function global du circuit discretise n'est PAS le meme qu'en analogique. La bilinear transform `s = (2/T)*(z-1)/(z+1)` comprime les frequences vers Nyquist. A 44.1k, une inductance de 5mH et un condensateur de 475pF resonent a une frequence warped differente qu'a 96k.

**Cherche** : Dans les travaux de Fettweis (1986), Werner (2016), Bernardini/Sarti, y a-t-il une methode pour "dewarper" les elements reactifs WDF pour obtenir une reponse frequentielle equivalente a differents sample rates ? L'approche de component value prewarping `L_w = L * tan(pi*f0/fs)/(pi*f0/fs)` est-elle applicable dans un arbre WDF avec non-linearites ?

### H3: Le Newton solver du JE-990 converge differemment selon le nombre de samples par cycle
A 44.1k, un cycle de 10kHz = 4.41 samples. Le Newton solver fait 1 iteration par sample (probe + commit). Avec seulement 4 points par cycle, la boucle n'a pas le temps de converger correctement a 10kHz. A 96k, 9.6 samples par cycle → meilleure convergence → +15 dB de gain restaure.

**Cherche** : Comment les implementeurs WDF (Brigola, Werner, D'Angelo) gerent-ils la convergence du solveur implicite a haute frequence / faible nombre de samples par cycle ? L'oversampling du solveur seul (multi-step Newton par sample) est-il une approche documentee ?

## Contraintes
- Budget CPU : <5% d'un coeur a 96 kHz, buffer 512 samples
- Pas de FFT dans le chemin audio
- Le J-A fait 3-4 iter NR normalement, max 16 en deep saturation
- L'oversampling global 2× doublerait le CPU
- L'oversampling du seul bloc transfo ou du seul Newton solver serait plus acceptable

## Ce que je cherche
1. **Publications academiques** sur la dependance au sample rate des modeles WDF nonlineaires et des modeles d'hysteresis magnetique discretises. Specifiquement : comment garantir l'invariance frequentielle d'un circuit WDF avec non-linearites implicites (J-A, BJT) quand le sample rate change ?
2. **Solutions industrielles** : comment UAD Luna, Arturia Pre 1973, Softube, Brainworx gerent-ils la variance sample rate dans leurs emulations de preamplis a transformateurs ? Oversampling fixe interne ? Component value scaling ? Coefficients tabulees par sample rate ?
3. **Analyse du -11 dB a 50 Hz** : si c'est le J-A qui change de Lm effectif entre 44.1k et 96k, quelle est la correction appropriee ? Normaliser les parametres J-A (Ms, k, a, c, alpha) en fonction de Ts ? Ou utiliser une methode d'integration d'ordre superieur (Runge-Kutta 4) pour le J-A qui serait moins sensible au pas ?
4. **Solution pour le JE990 10kHz +15 dB** : si c'est le Newton solver qui ne converge pas assez vite a haute frequence/bas sample rate, la solution est-elle (a) oversampling du Newton loop 2× ou 4× en interne, (b) multi-step Newton (2 iterations par sample au lieu de 1), ou (c) une autre approche ?
5. **Pseudo-code C++ concret** pour chaque solution recommandee, insertable dans l'architecture existante (WDF one-port CRTP, header-only, sample-par-sample processing)

---

# Metriques cibles

| Test | Metrique | Seuil | Actuel | Ecart |
|------|----------|-------|--------|-------|
| odt H3>H2 | H3/H2 linear | > 1.0 | 0.352 | besoin ×2.84 |
| SR mag Neve 1k | 96k/44k | ±1 dB | -2.02 dB | 1.02 dB |
| SR THD Neve | 96k/44k | 0.5–1.5 | 1.666 | 0.17 |
| SR THD JE990 | 96k/44k | 0.5–1.5 | 0.068 | 0.43 |
| SR 50Hz both | diff | ±2 dB | -11.2 dB | 9.2 dB |
| SR 10kHz JE990 | diff | ±2 dB | +15.4 dB | 13.4 dB |

# Format de reponse attendu
Pour chaque probleme : (1) diagnostic de la cause racine fonde sur la litterature, (2) solution concrete avec pseudo-code C++, (3) risques et effets de bord, (4) metriques de validation, (5) ordre d'implementation. Cite les sources.
