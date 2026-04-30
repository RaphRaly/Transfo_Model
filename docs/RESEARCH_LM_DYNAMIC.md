# Research — Lm(B) dynamique et "bloom" basse-fréquence

**Date** : 2026-04-29
**Statut** : note technique focalisée — fondation scientifique pour la modélisation du mécanisme #2 de `RESEARCH_MASTERING_DEPTH.md` (Lm(B) → bloom LF)
**Scope** : un seul mécanisme — Lm load-dependent et flux-dependent. Pas de digression sur H2, eddy, leakage, etc.
**Voir aussi** : `RESEARCH_MASTERING_DEPTH.md` §2.7 (overview), `archive/CHAN_MODEL_REFERENCE.md` (validation point SPICE)

---

## Légende — degrés de preuve

- 📚 **Littérature scientifique / canonique** — peer-reviewed, dérivation théorique, datasheet officielle, ouvrage de référence
- 🎚️ **Retours subjectifs documentés** — forums DIY, manuels constructeur, presse pro
- 🧠 **Hypothèse à tester** — extrapolation logique, ordre de grandeur estimé non validé empiriquement

---

## TL;DR

1. 📚 **µ_d (différentielle, instantanée) est la grandeur qui pilote Lm en audio**, pas µ_i ni µ_Δ standard. Le J-A scalaire fournit dM/dH analytique, donc µ_d est calculable sample-par-sample sans dérivation numérique.
2. 📚 **Paradoxe documenté** : pour matériaux dont µ pique loin (M6 silicon steel, pic vers 1.3 T), Lm **augmente** avec le niveau jusqu'au pic. Pour mu-metal (pic vers 0.3-0.4 T), on opère **autour ou au-delà du pic** donc Lm **diminue** avec le niveau. **Deux blooms sonores différents.**
3. 📚 **f_HP = R_eq / (2π·Lm)** avec **R_eq = R_source ∥ R_load_réfléchi**, pas R_source seul. Whitlock canonical.
4. 🧠 **Ordre de grandeur** : ratio Lm(0)/Lm(B_pic) estimé 2-5× sur mu-metal en régime mastering → f_HP peut osciller 1-25 Hz dans le même morceau selon enveloppe LF. À mesurer empiriquement.
5. 🧠 **Bloom = modulation conjointe amplitude + phase**, pas juste amplitude. Un compresseur LF parallèle ne le reproduit pas — phase à f_HP mobile manque.
6. 📚 **Approche DSP recommandée** : recalcul instantané Lm(B(t)) via dérivée J-A déjà disponible. Pas de filtre HP variable séparé. Validation industrielle : c'est l'architecture **Chan model** dans LTspice.
7. 📚 **Trou de littérature** : aucune mesure publiée systématique JAES de FR vs niveau pour transfos audio. Opportunité de bench mesure publiable.

---

## 1. Définitions précises des perméabilités

Le vocabulaire « perméabilité » dans la littérature audio est fréquemment utilisé de manière laxiste. Pour le code, il faut distinguer rigoureusement :

| Symbole | Nom | Définition formelle | Conditions |
|---|---|---|---|
| µ_i | Initiale | µ_i = lim_{H→0}(B/H) | H très petit, état désaimanté |
| µ_rev | Réversible | µ_rev = (∂B/∂H)\|_{H_DC, h_AC→0} | h_AC infiniment petit superposé à H_DC |
| µ_Δ | Incrémentale | µ_Δ = ΔB/ΔH autour d'un point de polarisation, h_AC petit mais fini | Régime petit-signal AC + bias DC |
| µ_anh | Anhystérétique | dérivée de M_an(H) (Langevin / J-A) | Hystérésis annulée (démagnétisation cyclique) |
| µ_d (ou µ_diff) | Différentielle | µ_d = dB/dH évaluée localement sur la trajectoire instantanée | Suit l'orbite B-H, hystérésis incluse |
| µ_eff | Effective | µ_eff(f, B_pic, charge) = Lm(f,...) · le / (N²·Ae) | Mesure indirecte sur composant complet |

📚 Sources : All About Circuits, Wikipedia *Permeability (electromagnetism)*, QuickField *Apparent and incremental permeability*, e-magnetica.pl *Types of permeability*.

### Quelle perméabilité pilote Lm en audio ?

📚 **Réponse : µ_d (différentielle), pas µ_i ni µ_Δ.** L'amplificateur impose v(t), le primaire intègre v(t) en flux Φ(t) = ∫v(t)dt, et le courant magnétisant i_m(t) résulte de la trajectoire B↔H sur la courbe d'hystérésis. La pente *instantanée* dB/dH le long de cette trajectoire est µ_d ; c'est elle qui fixe à chaque instant l'inductance vue par la source :

```
Lm(t) = N²·Ae·µ_d(B(t)) / le
      = N²·Ae·µ₀·(1 + dM/dH(t)) / le
```

🧠 µ_Δ (datasheet, "petit-signal autour d'un bias") et µ_d (instantanée le long du grand cycle) **coïncident seulement** si B_pic est petit devant la zone non-linéaire. En audio bus / mastering où B_pic peut atteindre 30-50 % de B_sat aux LF, ce n'est plus le cas et il faut µ_d. La distinction est rarement faite explicitement dans la littérature audio populaire qui parle vaguement de "inductance".

---

## 2. Mesures publiées de µ(B) sur alliages de transfos audio

### 2.1 Mu-metal / Permalloy 80% Ni

📚 **Composition** : 80-82% Ni, 4-5% Mo, balance Fe (HyMu80, MuMETAL®, Magnifer 7904)
📚 **Perméabilité DC** : µ_max = 80 000 - 350 000 selon recuit (HyMu80 minimum garanti 80 000 @ 40 G = 0.004 T)
📚 **Forme µ(B)** : pic ("knee") vers 0.3-0.5 T puis chute rapide ; B_sat ≈ 0.75-0.78 T
🧠 **Conséquence** : avec un transfo audio dimensionné pour B_pic_max ≈ 0.5·B_sat ≈ 0.4 T à la fréquence basse de référence, on opère **précisément autour du pic** et même au-delà pour les niveaux mastering — la sensibilité dµ_d/dB y est maximale.

Sources : Magnetic Shield Corp *MuMETAL Tech Data*, High Temp Metals *HyMu80*.

### 2.2 Permalloy 50 (50% NiFe / 1J85)

📚 µ_max plus bas (~30 000) mais B_sat plus haut (~1.5 T). Compromis classique pour transfos d'entrée de niveau pro où B_pic reste modéré.

### 2.3 Grain-oriented silicon steel (M6, 3% Si)

📚 µ_max ≈ 30 000-50 000 (orienté), pic vers 1.3 T (M6 atteint son maximum d'inductance à ≈13 000 G = 1.3 T selon retours diyAudio sur transfos de sortie tubes), B_sat ≈ 2.0 T.
📚 **Forme** : courbe µ(B) plus "plate", pic plus large que mu-metal. **Implication audio** : modulation Lm(B) plus douce mais commence dès les bas niveaux car µ_i déjà bas.

Sources : Edcor *M6 lamination*, Wikipedia *Electrical steel*.

### 2.4 Metglas 2714A (Co-based amorphe)

📚 B_sat = 0.57 T, µ@1kHz > 72 000 (peut atteindre 100 000 selon recuit)
📚 Loop carrée — µ_d quasi constant jusqu'à 0.4-0.5 T puis chute brutale. Comportement très différent de mu-metal : moins de modulation Lm(B) **avant** la saturation, mais transition plus brutale au-delà.

### 2.5 VITROPERM 500F (nanocristallin Fe-Si-B-Nb-Cu)

📚 µ_max ≈ 20 000 - 100 000+ selon recuit, B_sat = 1.2 T, taille de grain 10 nm
📚 Plage plate de µ(f) limitée à quelques 10 kHz. Bonne stabilité µ vs T.

### 2.6 Tableau récap qualitatif µ_d(B)

| Matériau | µ_max | B(µ_max) | B_sat | "Forme" du pic | Plage utile audio |
|---|---|---|---|---|---|
| Mu-metal 80 | 80k-350k | ~0.4 T | 0.75 T | Aigu, étroit | 0–0.3 T |
| 50% NiFe | 20-30k | ~0.6 T | 1.5 T | Moyen | 0–0.7 T |
| GOSS M6 | 30-50k | ~1.3 T | 2.0 T | Large, plat | 0–1.4 T |
| Metglas 2714A | 70-100k | très plat | 0.57 T | Loop carrée | 0–0.4 T |
| Vitroperm 500F | 20-100k | plat | 1.2 T | Modulable par recuit | 0–0.8 T |

🧠 **Implication produit** : un mode "mu-metal" doit produire la modulation Lm(B) la plus marquée (rapport µ_max/µ_i de 5-10× sur la plage utile), un mode "M6/silicon" la plus douce.

---

## 3. Comportement charge-dépendant — pourquoi Lm "voit" le secondaire

📚 **Modèle équivalent canonique** (Whitlock, ch. 11 *Handbook for Sound Engineers*) :

```
R_source ── L_leak_p ──┬── L_leak_s' ── R_load' ──
                       │
                       Lm  (parallèle)
                       │
                       R_core (loss)
                      GND
```

avec ' = grandeurs réfléchies au primaire (ratio N_p²/N_s²).

📚 **Fréquence de coupure HP (LF roll-off)** :

```
f_HP = R_eq / (2π · Lm)
```

avec **R_eq = R_source ∥ R_load_réfléchi**. Whitlock insiste : c'est la **résistance vue en parallèle de Lm**, pas R_source seul. Si R_load' = R_source, alors R_eq = R_source/2.

🎚️ **Conséquences pratiques** (Whitlock + Ballou *Handbook for Sound Engineers* + Lundahl WP ch. 5 + Sowter FAQ) :

- **Load infini (open)** : R_eq → R_source, f_HP minimum, Lm vue "pure", mais Q de la résonance leakage·Cp très haut → ringing HF
- **Load nominal** : f_HP doublée vs open, amortissement HF correct
- **Court-circuit** : R_eq → 0, f_HP → 0 (LF parfait) mais le primaire voit principalement la leakage, Lm court-circuité par le réfléchi nul

🧠 **Pour le plugin** : la "charge" côté secondaire d'un Manley sur le bus mastering n'est pas vraiment variable au sens hardware — c'est le préampli aval, ~10 kΩ. Mais simuler une "Output Load Z" comme contrôle utilisateur est légitime et change effectivement la f_HP de plusieurs Hz à plusieurs dizaines de Hz, audiblement.

---

## 4. Comportement flux-dépendant en dynamique

### 4.1 Variation intra-cycle

🎚️🧠 Les datasheets µ(B) sont quasi-DC ou petit-signal. En audio, sur un cycle 50 Hz à B_pic = 0.4 T sur mu-metal, µ_d(B) **change d'un facteur 3-10× au cours du même cycle** (par lecture de la courbe µ-B). Donc Lm n'est pas une scalaire ; c'est une fonction Lm(B(t)) recalculable à chaque sample.

### 4.2 Petit signal vs gros signal

🧠 Régime "petit signal" (B_pic < ~5 % B_sat) : µ_d ≈ µ_rev quasi-constante sur le cycle, Lm peut être traitée comme constante. Régime "gros signal" : Lm modulée fortement.

📚 **Transition** : il n'y a pas de fréquence-seuil unique ; c'est une transition d'amplitude. L'amplitude critique B_crit dépend du matériau (où dµ/dB devient significatif). Pour mu-metal, B_crit ≈ 0.05–0.1 T 🧠.

### 4.3 Lm complexe et tan(δ)

📚 En dynamique (Bertotti 1988, *Hysteresis in Magnetism*), les pertes eddy + excess ajoutent une composante imaginaire :

```
Z_m(jω) = jω·Lm' + Lm''(ω)
```

Lm' (réactif) chute avec f car µ' chute ; Lm'' (résistif équivalent) croît avec √f (excess) et f (eddy). VITROPERM 500F : µ(f) plat jusqu'à ~20 kHz, puis chute — au-delà ce n'est plus représentable par Lm scalaire.

🧠 **Pour le projet** : à fréquence audio basse (<1 kHz), Lm'' est faible et l'effet dominant est la modulation Lm'(B). À HF (>10 kHz) il faudrait Lm'(f). Pour le bloom LF, on peut négliger la dépendance fréquentielle de µ et se concentrer sur la dépendance B(t).

---

## 5. Modélisation J-A et lien avec Lm

📚 **Équation J-A** (Wikipedia *Jiles–Atherton model*, Jiles & Atherton 1986/1992) :

```
dM/dH = (1/(1+c)) · (M_an − M)/(δk − α(M_an − M))
       + (c/(1+c)) · dM_an/dH
```

avec M_an la magnétisation anhystérétique (Langevin), α le couplage inter-domaine, k la pinning, c la composante réversible, δ = ±1 selon dH/dt.

📚 **Lien à µ_d** : B = µ₀(H + M), donc

```
µ_d = µ₀·(1 + dM/dH)
```

C'est exactement ce que J-A produit. La dérivée dM/dH est dispo **analytiquement** dans la formulation (pas besoin de la différencier numériquement) — moins bruité côté DSP.

📚 **Limitations documentées** :
- Boucles mineures déformées sans correction (paramètre δM de Carpenter, ou approche modifiée — voir *Improved J-A SPICE* MDPI Electronics 2025)
- Susceptibilité incrémentale parfois négative (non-physique) — corrections via Bergqvist / Carpenter
- N'est précis sur la perméabilité réversible que si c est correctement identifié séparément
- Hauser 2004 ("Energetic model of ferromagnetic hysteresis") et la revue *Limitations of J-A models* (COMPEL 2024) signalent que J-A reproduit la boucle majeure mais peine sur les cycles asymétriques et minor loops, ce qui est précisément la situation audio (cycles asymétriques imbriqués)

📚 **Extensions Bertotti** : ajout de termes (k_eddy·dB/dt) et (k_excess·√|dB/dt|) au numérateur ou dénominateur (Zirka et al.). Permet la dépendance fréquentielle.

🧠 **Recommandation TWISTERION** : exposer µ_d(B) **dérivé du J-A déjà en place** comme primitive pour calculer Lm(t), plutôt qu'une LUT séparée. Garantit la cohérence physique entre la composante hystérétique (déjà modélisée) et la composante Lm.

---

## 6. Effet audio mesurable

🎚️ **Statement central diyAudio** (thread "Output transformer level dependent low frequency response") : *"Laminated core output transformers are nonlinear, with the primary inductance at low signal levels being **much lower** than at nominal maximum output level."*

Ce paradoxe (Lm **plus basse** à bas niveau pour M6 etc.) provient du fait que µ_i < µ_max : tant qu'on n'atteint pas le pic de µ, augmenter le niveau **augmente** Lm donc **descend** f_HP. **Au-delà du pic, c'est l'inverse.**

🎚️ **Sound on Sound / forums** : *"Transformer distortion is a function of level and frequency; the lower the frequency and the higher the signal level, the more distortion."* — corollaire direct de Faraday : à v fixé, B ∝ 1/f, donc à basse f on est plus profond dans la non-linéarité µ(B).

🎚️ **Manley Vari-Mu** (UA Manual + Manley manual PDF) : niveau opérationnel +4 dBu nominal, max +30 dBm, transfos "MANLEY nickel laminations", FR plate 20 Hz – 25 kHz **à un niveau donné**. HP filter 6 dB/oct -1 à -3 dB @ 100 Hz, -4 à -6 dB @ 50 Hz à niveau modéré. À +24 dBu (12.3 V_rms) sur primaire 600 Ω équivalent, B_pic ≈ 0.3-0.4 T à 30 Hz selon design 🧠 — clairement dans la zone modulée pour mu-metal.

🧠 **Ordre de grandeur de chute Lm(0) → Lm(B_max)** : pour mu-metal, ratio µ_d(0)/µ_d(0.4 T) typiquement **2-5×** dans le sens où µ_d culmine vers 0.3-0.4 T puis tombe sous µ_i au-delà. C'est-à-dire que Lm varie d'un facteur ~5 entre niveau bas et niveau pic mastering. Effet sur f_HP : si f_HP_nominale = 5 Hz, peut osciller entre 1 Hz (Lm haute) et 25 Hz (Lm basse) **dans le même morceau, en fonction de l'enveloppe LF**.

📚 **Mesure directe niveau-dépendante** : pas trouvée de publication systématique JAES sur ce point précis. Whitlock et Lundahl publient FR vs niveau dans leurs whitepapers mais souvent en distorsion (THD vs f, plusieurs niveaux), pas en réponse linéaire (FR à plusieurs niveaux, soustraite pour isoler la modulation linéaire). **C'est un trou de la littérature** — opportunité pour un papier mesure 🧠.

---

## 7. Lien physique → "bloom" perceptuel — prudence

📚 La psychoacoustique de la modulation d'amplitude est documentée mais **principalement à des taux de modulation rapides** (4-32 Hz, perception de rugosité/roughness). La perception de modulation lente (<1 Hz) du **gain LF** est moins étudiée directement.

📚 Seuils de détection AM : ~5 % à 8 Hz pour des sujets normaux (Nature Sci Reports 2024).

🎚️ **"Bloom"** dans le jargon mastering désigne : (i) une expansion perçue du bas-médium / basses sur les transitoires, (ii) une sensation de "sustain qui gonfle" sur les notes tenues. Le plugin oeksound Bloom s'inspire de ce comportement : "adaptive tone shaping" mappé sur les contours d'iso-loudness.

🧠 **Hypothèse à tester** : ce que les mastering engineers entendent comme "bloom" sur un transfo Manley/Cinemag est précisément la chute de f_HP avec le niveau (Lm augmente avec B sur la pente montante de µ(B) pour silicon ; Lm chute pour mu-metal au-delà du pic) → les LF passent **différemment** quand le signal est fort, donnant la sensation d'"expansion" ou de "compression musicale".

🧠 **Distinction "compression LF" vs "EQ dynamique"** : pas de littérature directe trouvée. Hypothèse :
- Compression LF = gain reduction in-band, perçue comme "atténuation pump"
- EQ dynamique de pente HP = changement de phase + amplitude conjoint → perception "gonflement / texture". **Phase est cruciale** ; un HP 6 dB/oct mobile produit un déplacement de phase à la fréquence de coupure qui n'est **pas** reproductible par compression scalaire

🧠 **Expérience à mener** : ABX entre (a) un compresseur LF parallèle reproduisant l'enveloppe de gain et (b) un HP variable suivant l'enveloppe à f_c modulée, sur sources musicales. Si les sujets distinguent → la phase compte → le modèle Lm(B) instantané est justifié.

---

## 8. Modélisation digitale du couplage Lm(B) → audio

| Approche | Fidélité | Coût | Phase instantanée correcte ? |
|---|---|---|---|
| **1.** HP variable suivant enveloppe | Moyenne | Faible | **Non** — perd la dépendance intra-cycle |
| **2.** Recalcul instantané Lm(B(t)) via J-A | Élevée | Moyen-haut | **Oui** |
| **3.** Réseau LR linéarisé par bins de niveau + interp | Moyen-haut | Moyen | Approximative |

📚 **LTspice / SPICE — état de l'art industriel** :
- **Modèle Chan** : 3 paramètres (Hc, Br, Bs), robuste, calcule µ_d à chaque pas — c'est **l'approche 2**. Limites : calage manuel des minor loops, parfois solver instable selon retours Analog Devices EZ
- **J-A SPICE amélioré** (MDPI Electronics 2025) : élimine la distorsion des minor loops, modulaire, reproduit Lm dynamique
- **Hodgdon model** : approche alternative, équations dérivées de la BH directement, moins répandue en simulation transfo audio

🧠 **Recommandation** : approche 2 (instantanée). Le J-A déjà en place fournit dM/dH analytique → µ_d(t) → Lm(t) → contribution courant magnétisant i_m(t). Le couplage avec Bertotti (déjà présent) reste cohérent. **Pas besoin d'un HP variable séparé** : la modulation LF émerge **naturellement** du circuit équivalent simulé sample-par-sample.

---

## 9. Causes secondaires à distinguer rigoureusement

| Phénomène | Effet sur LF dynamique | Comment le distinguer d'une modulation Lm(B) |
|---|---|---|
| **Saturation directe (B > B_sat)** | Clipping abrupt, harmoniques fortes | THD pic, clipping visible — différent d'un FR roll-off graduel |
| **Hystérésis loss** | Distorsion (H3, H5), pas de FR shift | Mesure THD à FR plate ; module amplitude harmonique pas l'amplitude fondamentale |
| **Eddy current loss (Bertotti K1)** | Lm complexe, chute µ' à HF (>5-10 kHz) | Effet HF, pas LF — visible sur FR @ HF, pas concerné par bloom |
| **Excess loss (Bertotti K2)** | Idem, plus douce, dépend √f | Distinguable de eddy par allure fréquentielle |
| **Capacités parasites** | Résonance HF (>20 kHz typique) | HF, pas LF |
| **Source impedance ≠ 0** | Décale f_HP, n'introduit pas de modulation niveau-dépendante | Mesure FR à plusieurs niveaux : si shift mais pas de modulation, c'est R_source |
| **Lm(B) modulation** | f_HP module avec niveau **et** phase dans la bande | Mesure FR à plusieurs niveaux → écart |

🧠 **Test diagnostique** : sweep LF à 3 niveaux (-10, 0, +10 dB par rapport au nominal). Si l'écart de FR à 30 Hz est > 1 dB et qu'il s'inverse ou se déforme selon le niveau, c'est Lm(B). Si l'écart est monotone ou constant, c'est probablement saturation/THD ou R_source.

---

## 10. Lien avec le modèle Chan (LTspice) — validation point industriel

Voir aussi `archive/CHAN_MODEL_REFERENCE.md` pour le détail.

### 10.1 Chan = approche 2 simplifiée

📚 **Architecture Chan** : `v(t) → ∫v dt = Φ(t) → B(t) = Φ/A → loi B-H hystérétique (Hc, Br, Bs) → H(t) → i(t) = H·le/N`

C'est exactement la causalité physique propre — **la tension impose le flux, le courant est la réaction magnétique**. Le J-A fait la même chose en plus fin (5+ params au lieu de 3, meilleure gestion des minor loops). En clair : **notre approche n'est pas exotique, elle raffine un standard SPICE éprouvé**.

> *"Chan model produces an apparent inductance that changes with level and signal history, instead of a fixed inductance."* — exactement le bloom recherché.

### 10.2 Architecture LTspice winding/core — applicable au DSP

📚 LTspice sépare explicitement :
- **Winding** (bobinage) : transformation idéale des variables électriques (v, i) en variables magnétiques (V_per_turn, ampère·tours)
- **Core** (cœur magnétique commun) : un seul nœud où s'accumulent les ampère·tours, où la loi B-H s'applique, et d'où émerge le V_per_turn renvoyé aux bobinages

Topologie :

```
[port primaire externe]
    ↓ Winding(N_p)
        ↓
        nœud cœur magnétique commun ←─── inductor Chan / J-A
        ↑
    ↑ Winding(N_s)
[port secondaire externe]
```

🧠 **Implication pour TWISTERION** : pour la cascade input + output (mécanisme #4 du master research), adopter cette séparation explicite — `WindingStage` (trivial, ratio + amp·tour) couplé à `CoreStage` (J-A + Bertotti existant). Évite de mélanger fuite, résistance cuivre et magnétisation dans un seul block monolithique.

### 10.3 Notation `Lm` — piège à connaître

⚠️ **Dans LTspice Chan, `Lm` = longueur moyenne du chemin magnétique** (paramètre géométrique).
**Dans notre code TWISTERION, `Lm` = inductance magnétisante** (variable électrique dynamique).

Quand on lit du SPICE en parallèle, ne pas confondre. Notre projet utilise `le` ou `path_length` pour la longueur géométrique (cf. `core/include/core/model/CoreGeometry.h`).

### 10.4 J-A vs Chan — pour notre cas (mastering nickel)

| Aspect | Chan | J-A (notre stack) |
|---|---|---|
| Paramètres matériau | 3 (Hc, Br, Bs) | 5+ (Ms, a, k, c, α) |
| Minor loops | Approximatif | Mieux (avec corrections type Carpenter) |
| Boucles dures (Br/Bs > 2/3, tape-wound) | **Solver bloque** | Stable |
| Boucles douces (NiFe, mu-metal) | OK | Stable |
| Asymétrie / DC bias / H_DC offset | Pas natif | **Déjà extensible** |
| Coût CPU | Faible | Moyen |

📚 LTwiki signale : *"problème numérique lorsque Br/Bs dépasse environ 2/3"*. Mu-metal, NiFe-50, GO SiFe (mastering audio) → Br/Bs ≈ 0.3-0.5 → **Chan OK mais J-A préférable** pour la finesse et les extensions H_DC.

### 10.5 Chan comme oracle de validation

🧠 **Avant** de mesurer M1 sur unité réelle (cher, demande prêt de Manley/Lundahl), **simuler en LTspice un transfo Chan** avec les paramètres équivalents → on a un oracle indépendant pour comparer notre J-A en termes d'écrasement Lm(B). C'est gratuit et reproductible. À utiliser comme validation point Sprint 0 du sprint plan.

---

## 11. Tableau récapitulatif — phénomène / équation / mesure / impact audio

| Phénomène | Équation gouvernante | Mesure cible | Impact audio attendu |
|---|---|---|---|
| µ_d(B) du matériau | µ_d = µ₀(1 + dM/dH) via J-A | BH loop quasi-DC, µ-B curve | Module Lm(t) intra-cycle, base du bloom |
| Lm(t) instantanée | Lm(t) = N²·Ae·µ_d(B(t))/le | Inductance vs courant à f ≪ f_audio | f_HP varie avec niveau |
| f_HP linéaire | f_HP = (R_src ∥ R_load')/(2π·Lm) | FR à plusieurs niveaux | Pente HP 6 dB/oct mobile en niveau |
| Bertotti dynamique | termes dB/dt ajoutés au J-A | Loop dyn vs f, à B_pic fixé | Lm complexe, pertes HF, peu d'effet LF |
| Bloom perçu | Modulation conjointe ampl + phase LF | ABX vs compresseur scalaire | "Expansion" sustain, gonflement basses |
| Charge dépendance | R_eq = R_src ∥ R_load' | FR vs Z_load secondaire | Décalage f_HP (Hz à dizaines de Hz) |
| Saturation hard | B atteint B_sat | THD vs niveau, clipping | Harmoniques abruptes, distinct du bloom |

---

## 12. Conclusions opérationnelles

1. **µ_d (instantanée) est la grandeur qui pilote Lm en audio**, pas µ_i ni µ_Δ standard. Le J-A déjà en place fournit dM/dH analytique → utiliser cette dérivée pour calculer Lm(t) sample-par-sample.

2. **L'asymétrie µ_d(B) avant/après le pic** explique le paradoxe documenté : mu-metal → Lm chute avec niveau (on est au-delà du pic) ; M6/silicon → Lm monte avec niveau (on monte vers le pic). **Deux blooms distincts.**

3. **f_HP = R_eq / (2π·Lm)** avec R_eq = R_source ∥ R_load_réfléchi. Exposer une "Output Load Z" comme contrôle utilisateur change la coupure de manière musicalement utile et physiquement honnête.

4. **Le "bloom" est très probablement une modulation conjointe amplitude + phase LF**, distincte d'une compression scalaire — phase importante. **Approche 2 (instantanée via J-A) la capture ; approche 1 (HP variable suivant enveloppe) ne capture que l'amplitude.**

5. **Trous de littérature** : aucune mesure publiée systématique JAES de FR vs niveau pour transfos audio. Projet potentiel de bench mesure pour valider le modèle.

6. **Validation industrielle** : le modèle Chan (LTspice) fait exactement la même chose conceptuellement — on raffine un standard, on n'invente pas. Architecture winding/core de LTspice à reprendre pour la cascade DSP.

7. **Tags 🧠 à valider** : ratio Lm(0)/Lm(B_max) en mu-metal (estimé 2-5×), B_pic réel sur Vari-Mu à +24 dBu (estimé 0.3-0.4 T), équivalence perceptuelle bloom ↔ modulation phase + amplitude. Voir protocole §6 de `RESEARCH_MASTERING_DEPTH.md`.

---

## Sources

### Définitions & théorie
- [All About Circuits — Permeability definitions](https://www.allaboutcircuits.com/technical-articles/nonlinear-magnetization-curves-core-saturation-and-a-review-of-permeability-definitions/)
- [Wikipedia — Permeability (electromagnetism)](https://en.wikipedia.org/wiki/Permeability_(electromagnetism))
- [QuickField — Apparent and incremental permeability](https://quickfield.com/help66/QuickField.chm/html/Theory/ApparentAndIncrementalMagneticPermeabilities.htm)
- [e-magnetica — Types of magnetic permeability](https://www.e-magnetica.pl/doku.php/types_of_magnetic_permeability)
- [Wikipedia — Jiles–Atherton model](https://en.wikipedia.org/wiki/Jiles%E2%80%93Atherton_model)
- [Wikipedia — Mu-metal](https://en.wikipedia.org/wiki/Mu-metal)
- [Wikipedia — Electrical steel](https://en.wikipedia.org/wiki/Electrical_steel)

### Transfos audio canoniques
- [Whitlock — Audio Transformers (Jensen)](https://www.jensen-transformers.com/wp-content/uploads/2014/08/Audio-Transformers-Chapter.pdf)
- [Lundahl WP Chapter 1 — Intro](https://lundahltransformers.com/wp-content/uploads/datasheets/PSW_WhitePaper_Download_Chapter_1.pdf)
- [Lundahl WP Chapter 5 — Impedance](https://www.lundahltransformers.com/wp-content/uploads/datasheets/PSW_WhitePaper_Download_Chapter_5.pdf)
- [Sowter FAQ — Transformer impedance](https://www.sowter.co.uk/faq.php)
- [diyAudio — Output transformer level dependent LF response](https://www.diyaudio.com/community/threads/output-transformer-level-dependent-low-frequency-response.382983/)
- [diyAudio — Surprising transformer harmonic distortion](https://www.diyaudio.com/community/threads/surprising-transformer-harmonic-distortion-measurements.418109/)
- [Manley Variable Mu manual (UA)](https://help.uaudio.com/hc/en-us/articles/18737967080340-Manley-Variable-Mu-Manual)

### Datasheets matériaux
- [Magnetic Shield Corp — MuMETAL Tech Data](https://www.magnetic-shield.com/mumetal-technical-data/)
- [HighTempMetals — HyMu80](https://www.hightempmetals.com/techdata/hitempHymu80data.php)
- [Edcor — M6 lamination](https://edcorusa.com/pages/m-6-steel-lamination)
- [Metglas 2714A datasheet](https://metglas.com/wp-content/uploads/2021/06/2714A-Magnetic-Alloy-updated.pdf)
- [VAC Vitroperm 500F](https://vacuumschmelze.com/products/soft-magnetic-materials-and-stamped-parts/nanocrystalline-material-vitroperm)
- [VAC Chokes & Cores datasheet (Mouser)](https://www.mouser.com/pdfdocs/VACChokesandCoresDatasheet.pdf)

### J-A & extensions Bertotti
- [Tandfonline — Modelling dynamic hysteresis loops with J-A (Zirka et al.)](https://www.tandfonline.com/doi/full/10.1080/13873950802432016)
- [ScienceDirect — Frequency-dependent J-A model](https://www.sciencedirect.com/science/article/abs/pii/S0921452615000617)
- [AIP — Physical aspects of J-A models](https://pubs.aip.org/aip/jap/article/112/4/043916/939438/On-physical-aspects-of-the-Jiles-Atherton)
- [COMPEL — Limitations of J-A models 2024](https://www.emerald.com/compel/article-abstract/43/1/66/1216966/Limitations-of-Jiles-Atherton-models-to-study-the)
- [MDPI Electronics 2025 — Improved J-A SPICE](https://www.mdpi.com/2079-9292/15/5/1009)

### Chan / LTspice
- [LTwiki — Chan model](https://ltwiki.org/index.php?title=The_Chan_model)
- [LTwiki — Transformers](https://ltwiki.org/?title=Transformers)
- [All About Circuits — Simulating non-linear transformers in LTspice](https://www.allaboutcircuits.com/technical-articles/simulating-non-linear-transformers-in-ltspice/)

### Psychoacoustique
- [Nature Sci Reports — Psychoacoustic AM detection 2024](https://www.nature.com/articles/s41598-024-58225-1)
- [PubMed 40358992 — Neural & psychoacoustic AM processing](https://pubmed.ncbi.nlm.nih.gov/40358992/)
- [oeksound Bloom plugin](https://oeksound.com/plugins/bloom/)
- [Sound on Sound — oeksound Bloom review](https://www.soundonsound.com/reviews/oeksound-bloom)
