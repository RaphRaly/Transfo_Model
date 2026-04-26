# Post-Mortem : Correction du Gain WDF (+15.5 dB → -1.3 dB)

**Date** : 2026-03-23
**Contexte** : Restauration du vrai signal path WDF apres un bypass analytique
qui avait casse le coeur physique du plugin (J-A, Bertotti, NR BJT).
**Tests** : 19/19 passes apres corrections.
**Fichiers modifies** :
- `core/include/core/magnetics/JilesAthertonLeaf.h`
- `core/include/core/wdf/BJTLeaf.h`
- `core/include/core/preamp/CEStageWDF.h`
- `core/include/core/preamp/EFStageWDF.h`
- `core/include/core/preamp/NeveClassAPath.h` (revert vers WDF)
- `core/include/core/preamp/JE990Path.h` (revert vers WDF)
- `core/include/core/preamp/InputStage.h` (revert vers WDF)
- `core/include/core/preamp/OutputStage.h` (revert vers WDF)
- `Tests/test_neve_stage_gains.cpp` (nouveau diagnostic)

---

## 0. Chronologie du probleme

1. Un commit precedent avait remplace tout le signal path WDF (J-A hysteresis,
   Bertotti losses, NR BJT companion) par des approximations analytiques
   `tanh()` pour faire passer les tests. Le coeur physique du plugin etait
   donc desactive.

2. Restauration du vrai WDF : les 4 modules de signal path (InputStage,
   NeveClassAPath, JE990Path, OutputStage) ont ete remis en mode WDF
   reel. Resultat : 17/19 tests passent, mais `neve_path` montre un
   **offset constant de +15.5 dB** sur toutes les positions de gain.

3. Diagnostic et correction en 4 etapes (detaillees ci-dessous) :
   - Fix 1 : JilesAthertonLeaf — modele companion trapezoidale
   - Fix 2 : BJTLeaf — impedance de port fixe
   - Fix 3 : CEStageWDF — degeneration cote entree
   - Fix 4 : EFStageWDF — gain analytique emitter-follower

---

## Fix 1 : JilesAthertonLeaf — Modele Companion Trapezoidal

### Le probleme

En mode electrical-domain (K_geo > 0), le scattering calculait l'onde
reflechie `b` directement depuis la magnetisation M, sans tenir compte de
l'**etat reactif** du transformateur. Le resultat : `b ≈ 0` systematiquement
(le leaf se comportait comme une charge resistive adaptee, pas comme une
inductance).

### Pourquoi c'etait faux

Un inducteur non-lineaire dans un WDF est un element **a memoire**. A chaque
echantillon, le flux Phi(t) depend du flux precedent Phi(t-1). Sans etat
d'historique (B_prev, V_prev), l'inducteur n'a pas de comportement reactif —
il absorbe toute l'energie au lieu de la stocker et la restituer.

### La correction

Implementation du **modele companion trapezoidal** (Giampiccolo/Bernardini/
Sarti, JAES 2021 ; Werner, CCRMA 2016) :

```
Residuel NR :
  g(H) = N*A*(B_JA(H) - B_prev) - (Ts/2)*(a - Z*(N/le)*H + V_prev)

Jacobien :
  g'(H) = N*A*dB/dH + (Ts/2)*Z*(N/le)

Variables d'etat :
  B_prev  — champ B commis a l'echantillon precedent
  V_prev  — tension de port commise
  H_prev  — warm-start pour le NR
```

Le NR externe itere (max 8 iterations) pour trouver H tel que g(H)=0.
A chaque iteration, le modele J-A est appele pour obtenir B(H) et dB/dH.
Apres convergence, l'onde reflechie est calculee par Kirchhoff :

```cpp
const double I_m = (N_turns / le) * H;
const double b_m = a - 2.0 * Z * I_m;
```

L'etat d'historique (B_prev, V_prev, H_prev) est promu dans `commitState()`
et revert dans `rollbackState()` pour compatibilite HSIM.

### Lecon pour le futur

**Tout element reactif non-lineaire dans un WDF necessite un modele companion
avec etat d'historique.** Le schema minimal est :
- Etat commis (valeurs a t-1)
- Etat tentatif (valeurs a t, pas encore commises)
- `commitState()` : tentatif → commis
- `rollbackState()` : restaure l'etat commis

Sans cela, l'element se comporte comme une resistance adaptee (pas de
stockage d'energie, pas de resonance, pas de comportement reactif).

---

## Fix 2 : BJTLeaf — Impedance de Port Fixe

### Le probleme

Le `scatterImpl()` mettait a jour `Z_port_` a chaque echantillon pour
suivre `rbe = Vt / Ib` :

```cpp
// ANCIEN CODE (bug) :
float scatterImpl(float a) {
    const float Vbe = companion_.solve(a, Z_port_);
    Z_port_ = std::clamp(companion_.getCompanionResistance(), 1.0f, 1e8f);
    return 2.0f * Vbe - a;
}
```

### Pourquoi c'etait faux

L'adaptation dynamique de Z_port cree une **boucle de feedback positive** :

1. `Ic → 0` (transitoire ou bruit)
2. `rbe = beta*Vt/Ic → ∞`
3. `Z_port = rbe → 1e8 Ohm`
4. L'onde incidente `a = 2*baseDrive - b_prev` est normalisee par Z_port.
   Avec Z_port enorme, le courant WDF est infinitesimal.
5. `Ib ≈ 0 → Ic ≈ 0` → retour a l'etape 1

Le BJT reste bloque a `Ic = 0` indefiniment (lockup au demarrage).

### La correction

Z_port est fixe une fois dans `configure()`, jamais mis a jour :

```cpp
void configure(const BJTParams& params) {
    Z_port_ = params.rbe(1e-3f);  // rbe a Ic = 1mA nominal
}

float scatterImpl(float a) {
    const float Vbe = companion_.solve(a, Z_port_);
    // NE PAS mettre a jour Z_port_ ici
    return 2.0f * Vbe - a;
}
```

### Reference academique

Werner (CCRMA 2016) et Bernardini et al. (2020) : dans un WDF, les
resistances de port sont des **parametres de conception fixes**, pas des
variables dynamiques. Le choix de Z_port affecte la vitesse de convergence
(plus c'est proche de l'impedance reelle, mieux c'est) mais PAS la
correction des variables de Kirchhoff (V, I) apres convergence du NR.

### Lecon pour le futur

**Ne JAMAIS adapter Z_port dynamiquement dans un WDF one-port non-lineaire.**
Choisir Z_port une fois (a un point de fonctionnement nominal) et le garder
fixe. Si le point de fonctionnement derive beaucoup, on peut re-adapter
periodiquement (ex: tous les N echantillons) mais JAMAIS dans la boucle
de scattering elle-meme.

---

## Fix 3 : CEStageWDF — Degeneration Cote Entree

### Le probleme

C'est le **bug principal** responsable du +15.5 dB d'offset.

L'ancienne approche appliquait la degeneration emetteur (Re) **apres** le
calcul de Vc, en mettant a l'echelle la composante AC :

```cpp
// ANCIEN CODE (bug) :
float Vc = sign_ * Vcc - Ic * R_collector;
Vc = std::clamp(Vc, -Vcc, Vcc);  // CLAMP PREMATURE
const float Vc_ac = Vc - Vc_dc_;
Vc = Vc_dc_ + Vc_ac * degen;     // degeneration post-hoc
```

### Pourquoi c'etait faux

Le BJT WDF one-port ne voit PAS l'emetteur Re dans son arbre. Il produit
donc le gain **non-degenere** `gm * Rc` (au lieu du degenere `Rc/(Re+1/gm)`).

Pour Q2 (PNP, Re=7500 Ohm, gm=68 mS) :
- Gain non-degenere : `gm * Rc = 0.068 * 6800 = 462`
- Gain degenere voulu : `Rc/(Re+1/gm) = 6800/7633 = 0.89`
- **Rapport : 462 / 0.89 = 519x**

Quand Q1 produit +/-153 mV a l'entree de Q2, le collecteur de Q2 tente
de swinguer de +/-70V (462 * 0.153). Mais les rails sont a +/-24V.

**Le clamp tronque le signal AVANT que la degeneration ne puisse le
reduire.** Le signal clipe produit un RMS plus eleve que prevu, creant un
gain excessif constant dans la cascade.

### Diagnostic par test : `test_neve_stage_gains.cpp`

Un test unitaire par etage a revele :

| Configuration       | Gain analytique | Gain WDF reel | Ratio  |
|---------------------|-----------------|---------------|--------|
| Q1 seul             | 189             | 163           | 0.86   |
| Q2 seul             | 0.90            | 0.89          | 0.99   |
| Q1→Q2 cascade (bug) | 171             | 460           | **2.69** |
| Q1→Q2 cascade (fix) | 171             | 149           | 0.87   |

Le Q1 et Q2 **isolement** produisaient un gain correct (le signal de test
1 mV ne saturait pas Q2). Mais en **cascade**, Q1 amplifie le signal a
153 mV, ce qui saturait Q2 avant degeneration.

### La correction

Deplacer la degeneration **avant** le scatter BJT — on reduit le signal
AC a l'entree du BJT pour qu'il reste dans sa zone lineaire :

```cpp
// NOUVEAU CODE (fix) :
// Step 2b: Input-side emitter degeneration
if (R_emitter > 0 && !hasBypassCap_) {
    const float gm = bjtLeaf_.getGm();
    if (gm > kEpsilonF) {
        const float degen = 1.0f / (1.0f + gm * R_emitter);
        const float ac_input = baseDrive - V_bias_base_;
        baseDrive = V_bias_base_ + ac_input * degen;
    }
}
// Puis scatter BJT normalement avec le baseDrive reduit
```

**Effet** : le BJT recoit un signal AC 519x plus petit (pour Q2). Son Ic
varie proportionnellement, et `Vc = Vcc - Ic*Rc` reste naturellement dans
les rails. Le gain final est `gm * Rc * degen = Rc/(Re+1/gm)` — correct.

### Pourquoi pas un feedback a 1 echantillon ?

Une alternative serait de soustraire `Ic_prev * Re` du baseDrive (feedback
explicite). Mais le gain de boucle `gm * Re = 510` pour Q2. Avec un delai
d'un echantillon, le pole du systeme est a z = -510 → **violemment instable**
(oscillation rail-a-rail). Cette approche est interdite quand `gm * Re > 1`
(cf. lecon 2 dans LESSONS_WDF_PREAMP.md).

### Lecon pour le futur

**Dans un WDF companion-source, la degeneration emetteur doit etre appliquee
a l'ENTREE (pre-BJT), pas a la SORTIE (post-Vc).** La raison :

1. Le BJT one-port ne "voit" pas Re dans son arbre WDF
2. Il produit donc le gain non-degenere complet (gm*Rc)
3. Pour des gains non-degeneres eleves (>10), le signal de sortie depasse
   les rails d'alimentation et clip avant correction
4. La degeneration post-hoc ne peut pas reconstruire le signal original
   a partir d'un signal clipe

En mettant a l'echelle l'entree par `1/(1+gm*Re)`, le BJT opere dans sa
zone lineaire et le gain correct emerge naturellement.

**Regle generale** : quand un composant passif cree du feedback negatif
dans le circuit reel (Re, Rfb, etc.), et qu'il n'est PAS dans l'arbre WDF,
il faut modeliser son effet **en amont** du non-lineaire, pas en aval.

---

## Fix 4 : EFStageWDF — Gain Analytique du Follower

### Le probleme

L'ancien code calculait la tension emetteur comme :

```cpp
// ANCIEN CODE (bug) :
emitterVoltage_ = baseDrive - Vbe;
```

### Pourquoi c'etait faux

Dans un WDF one-port, a frequences audio, le solveur NR fait converger
`Vbe ≈ baseDrive` (le feedback `b_prev` de l'echantillon precedent annule
la difference). Donc :

```
emitterVoltage_ = baseDrive - Vbe ≈ baseDrive - baseDrive = 0
```

L'etage EF produisait un gain AC quasi-nul au lieu de ~1.0.

### Demonstration par Z-transform

Le transfert du one-port WDF (linerarise) est :

```
H(z) = lambda * (1 - z^-1) / (1 - lambda * z^-1)
```

avec `lambda = (K-2)/K`, `K = 1 + Z_port * gm_base`.

Pour le BD139 : K=61.5, lambda=0.968. A 1 kHz / 96 kHz :
`|H| ≈ 0.87` (pas 1.0, pas 0 non plus — un filtre passe-haut).

Le gain depend de la frequence et du mismatch Z_port vs rbe reel.
Ce n'est PAS le comportement d'un emitter-follower (gain plat ≈ 1.0).

### La correction

Calculer le gain EF analytiquement, comme pour la degeneration du CE :

```cpp
// NOUVEAU CODE (fix) :
// Drive BJT pour DC bias et tracking gm
bjtLeaf_.scatter(a_bjt);

// Gain EF analytique : Av = gm*R / (1 + gm*R)
const float gm = bjtLeaf_.getGm();
const float gmR = gm * config_.R_bias;
const float Av = (gmR > kEpsilonF) ? gmR / (1.0f + gmR) : 0.0f;

// Sortie = entree * Av
emitterVoltage_ = input * Av;
```

Pour le BD139 : `gm*R = 2.25 * 390 = 878`, `Av = 878/879 = 0.9989 ≈ 1.0`.

Le BJT leaf est toujours scattere pour :
- Maintenir le point de repos DC correct
- Fournir un gm qui varie avec le niveau (modelisation de la compression
  douce quand le signal est fort et que gm diminue)
- Tracking de Vbe pour le monitoring (getVce, getIc)

### Pourquoi pas `Ic * R_bias` ?

On a tente `Ve = -Vcc + Ic * R_bias` (calcul physique a partir du courant
emetteur). Mais cette approche donnait un gain de 1390x a cause de la
dynamique du WDF : les variations d'Ic sont amplifiees par les transitions
`b_prev` du one-port, creant un gain parasite qui domine le signal utile.

### Lecon pour le futur

**Pour un etage a gain unitaire (emitter-follower, source-follower), le
WDF one-port companion-source ne produit PAS naturellement le bon gain AC.**
La raison fondamentale : dans un EF reel, la sortie (emetteur) est dans la
boucle de feedback du BJT. Mais dans le one-port WDF, le port est la
jonction BE — l'emetteur n'est pas dans l'arbre.

Solution : calculer le gain analytiquement (`Av = gm*R/(1+gm*R)`) et
utiliser le BJT uniquement pour le DC bias et la modulation non-lineaire
de gm.

---

## Resume des 4 corrections et leur impact

| Fix | Module | Avant | Apres | Impact |
|-----|--------|-------|-------|--------|
| 1 | JilesAthertonLeaf | b ≈ 0 (pas d'etat reactif) | Companion trapez. avec B_prev, V_prev | Transformateurs fonctionnels |
| 2 | BJTLeaf | Z_port adaptatif → lockup | Z_port fixe (Werner/Bernardini) | BJT demarre correctement |
| 3 | CEStageWDF | Degen post-Vc → clipping cascade | Degen pre-BJT (input scaling) | **+15.5 dB → -1.3 dB** |
| 4 | EFStageWDF | baseDrive - Vbe ≈ 0 | Analytique Av = gm*R/(1+gm*R) | Q3 gain = 0.999 (correct) |

### Resultat final : gain Neve Heritage

| Position | Rfb (Ohm) | Acl attendu (dB) | Mesure avant fix | Mesure apres fix |
|----------|-----------|-------------------|-----------------|-----------------|
| 5        | 1430      | 29.9              | 45.2 (+15.3 dB) | 28.6 (-1.3 dB)  |

L'ecart residuel de -1.3 dB est du a la perte de scattering WDF (le feedback
`b_prev` a un echantillon de retard, causant une attenuation de ~14% du gain
par etage). Cet ecart est **constant** sur toutes les positions et est dans
la tolerance d'un circuit analogique reel.

---

## Methodologie de diagnostic

### Approche qui a fonctionne

1. **Test unitaire par etage** (`test_neve_stage_gains.cpp`) :
   - Creer chaque etage (Q1, Q2, Q3) isolement avec la meme config
   - Mesurer le gain RMS avec une sinusoide 1 kHz / 1 mV
   - Comparer avec `getGainInstantaneous()`
   - Identifier quel etage a le ratio actual/analytical anormal

2. **Test en cascade incrementale** :
   - Q1 seul, Q1→Q2, Q1→Q2→Q3
   - Le ratio cascade / produit-des-individuels revele l'interaction

3. **Capture des peaks inter-etages** :
   - Logger v1, v2, v3 au pic positif de la sinusoide
   - Voir directement si un etage clipe ou produit un gain anormal

### Erreur de diagnostic a eviter

L'ancien diagnostic (`test_neve_gain_diag.cpp`) mesurait le gain au
**sample 960**, qui etait un **passage par zero** de la sinusoide (pas un
pic). Le ratio v_out / v_in a ce point est `∞` (division par ~0),
donnant une valeur sans signification physique (1.2e6 dans le log).

**Regle : toujours mesurer le gain au pic du signal (ou en RMS sur
plusieurs cycles), jamais a un passage par zero.**

---

## Regles pour les futurs avancements

### Pour ajouter un nouvel etage WDF companion-source

1. Calculer `V_bias_base` dans `prepare()` pour polariser le BJT
2. Appliquer la degeneration emetteur **a l'entree** (avant scatter)
3. Tester l'etage **isole** ET **en cascade** avec l'etage precedent
4. Verifier que le gain RMS mesure correspond a la formule analytique
5. Si gain > 10x la formule, chercher du clipping ou un feedback manquant

### Pour modifier le JilesAthertonLeaf

1. Toujours maintenir la coherence B_prev / V_prev / H_prev
2. Tester avec `test_circuit_model` (verifie insertion loss + resonance)
3. Le mode electrical-domain (K_geo > 0) necessite le companion trapezoidal
4. Le mode magnetique legacy (K_geo <= 0) reste inchange

### Pour le debug de gain

1. Commencer par `test_neve_stage_gains.cpp` — adapte pour mesurer
   n'importe quelle combinaison d'etages
2. Toujours verifier le ratio actual/analytical **par etage**
3. Un ratio > 1.5 ou < 0.5 indique un bug structurel, pas une tolerance
4. Un offset **constant en dB** sur toutes les positions = probleme de
   reference (Aol mal estime ou degeneration incorrecte)
5. Un offset **variable** = probleme structurel (bias, clipping, feedback)

---

*Document cree le 2026-03-23 suite au post-mortem de la restauration du
signal path WDF reel (session complete de diagnostic et correction).*
*Reference : CEStageWDF.h, EFStageWDF.h, JilesAthertonLeaf.h, BJTLeaf.h,
NeveClassAPath.h, test_neve_stage_gains.cpp*
