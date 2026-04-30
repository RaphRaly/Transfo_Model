# Research — "Depth" sonore des transformateurs en mastering

**Date** : 2026-04-28
**Auteur** : recherche agent (sources web + sandbox), compilée par Claude
**Statut** : **note de cadrage R&D — hypothèses prioritaires structurées**, pas démonstration scientifique. Voir §6 pour le protocole de validation requis avant d'engager les sprints.
**Scope** : fondation pour `SPRINT_PLAN_MASTERING.md`

---

## Légende — degrés de preuve

Chaque claim ci-dessous est tagué selon son degré de preuve, pour ne pas confondre cadrage et démonstration :

- 📚 **Littérature scientifique** — dérivation théorique, mesure publiée peer-reviewed
- 🎚️ **Retours subjectifs documentés** — reviews, interviews, datasheets fabricant. Utiles pour l'orientation produit, ne valent pas preuve mécanistique
- 🧠 **Hypothèse à tester** — extrapolation logique du modèle physique ou intuition non encore validée empiriquement

> **Note importante** : les reviews hardware (Tape Op, SOS, Gearspace) sont utiles pour l'orientation **subjective** visée mais ne décomposent pas le mécanisme physique interne. Manley, RND et Marinair ne publient pas de décomposition noyau/pertes/biais. Toute corrélation "review → mécanisme" est extrapolation à valider.

---

## TL;DR

La "depth" mastering est probablement **l'émergence collective de plusieurs phénomènes** plutôt qu'un mécanisme unique. Hypothèses prioritaires (à tester via §6) :

1. 🧠 **Lm grande, flux-dependent** — candidat fort pour le "bloom" LF + dynamic EQ naturel sub-200 Hz. Mécanisme physique 📚 dérivable du J-A, contribution perceptuelle à mesurer A/B.
2. 🧠 **Asymétrie B-H / DC bias** — candidat pour le H2 even-order. La psychoacoustique du H2 et la perception de profondeur est **soutenue par Katz** 📚, mais le lien causal "asymétrie de transfo mastering → H2 → depth" reste **à démontrer empiriquement**.
3. 🧠 **Hystérésis active loin de saturation** — niveau de fonctionnement bas (estimation typique: une fraction de Bsat, à mesurer sur unités de référence). Signature subjective documentée 🎚️ (Vari-Mu +30 dBu, <0.1 % THD) ; lien causal "hystérésis petit-signal → 3D image" non démontré scientifiquement.
4. 🧠 **Cascading input + output transfo** — cross-modulation pair × pair = pair, hypothèse théorique cohérente, pas de mesure publiée à ma connaissance.

**Espérance** : le moteur actuel (J-A + Bertotti + Lm + LC) **devrait** capturer une part significative de ces effets moyennant 3 ajouts simples (`H_DC` offset, flag topologie, cascade explicite). Le **% de couverture est à valider en A/B perceptuel**, pas à asserter.

À éviter par ROI : Preisach asymétrique full, vector hysteresis 2D, magnétostriction.

---

## 1. Définition acoustique/perceptuelle de la "depth"

Bob Katz (*Mastering Audio: The Art and the Science*, ch. "How to Achieve Depth and Dimension") 📚 décrit la "depth" comme la combinaison de **trois phénomènes psychoacoustiques** :

1. **Front-to-back perspective** 📚 — placement de sources à différentes distances. Repose sur les indices de réverbération précoce et le ratio direct/réverbéré (consensus psychoacoustique). 🧠 *Hypothèse complémentaire non démontrée par Katz* : les **micro-décalages de phase fréquence-dépendants** induits par les transfos peuvent y contribuer, mais cette causalité reste à mesurer (mesure de phase NL vs niveau requise).
2. **Low-end weight / "bloom"** 🎚️ — sensation de poids et d'épanouissement du grave, distincte d'un boost EQ. 🧠 *Hypothèse mécanistique* : provient de la modulation de l'amplitude LF par le niveau (Lm load-dependent), créant une compression "musicale" sub-200 Hz. Cohérent avec la dérivation J-A 📚 mais **lien causal mécanisme→perception "bloom" à valider** par mesure A/B.
3. **3D image / "size"** 🎚️ — élargissement et profondeur de champ. 📚 Katz qualifie le H2 de *"sonic gold for audiophiles, [responsible for] warm sound quality, three-dimensionality, and beautiful reproduction of ambience and depth."* — son article documente l'effet d'**un processeur ajoutant explicitement du H2 type triode**. 🧠 *Extrapolation à tester* : que les transfos mastering produisent cet effet **principalement via asymétrie B-H** plutôt que via leakage L+C, choix matériau ou autre mécanisme — cette hiérarchie causale reste hypothétique.

### 1.1 Différences mic-pre vs mastering grade

| Paramètre | Mic-pre (JT-115K-E, T1444) | Mastering (Manley MP, Vari-Mu, RND MBP) |
|---|---|---|
| Niveau opérationnel | -40 à +4 dBu | +4 à +24 dBu, jusqu'à +30 dBu |
| Headroom flux (Bsat) | ~1.0–1.4 T (silicon) ou ~0.7 T (nickel) | Idem en T mais **noyaux beaucoup plus gros** → flux pic à niveau nominal très en dessous du genou |
| Lm magnétisante | 1–3 H | **10–50 H** (gros noyaux nickel, beaucoup plus de tours) |
| Ratio | Step-up 1:5 à 1:10 (boost) | 1:1 ou 1:2 (driver de bus, pas de gain) |
| Régime sonique | Saturation occasionnelle, transients | **Hystérésis active en permanence mais loin de saturation hard** |

**Point clé** : un transfo de mastering n'est presque jamais en saturation. Toute la "depth" vient donc du régime petit-signal hystérétique, des pertes excess et du comportement Lm(B) — pas de la saturation en clipping.

---

## 2. Mécanismes physiques candidats — contribution à la "depth"

### 2.1 Inductance magnétisante Lm grande et load-dependent — priorité **HAUTE**

📚 **Mécanisme physique** : pour un transfo bus mastering avec source ~600 Ω et Lm ~30 H, f_HP = R/(2π·Lm) ≈ 3 Hz, soit **~45° à 3 Hz et 5–10° à 50 Hz** (dérivation directe). Lm est **load-dependent et flux-dependent** : à fort niveau, µ_Δ chute, Lm chute, le pôle remonte vers 10–20 Hz. La modulation de l'amplitude LF par le niveau est dérivable du J-A.

🎚️ Confirmé par diyAudio : *"Changes in primary inductance have an effect on the low frequency cutoff of the whole amplifier."*

🧠 **Lien causal "modulation Lm(B) → bloom perçu"** : cohérent et plausible, mais **non démontré perceptuellement**. À valider par A/B sur signal mastering réel avec/sans modulation Lm exposée.

Dans le modèle existant : `Lm = N²·µ_Δ(B)·Ae/le` où µ_Δ vient de la dérivée locale J-A — **déjà capturé partiellement**, à exposer explicitement dans le rendu audio.

### 2.2 Asymétrie B-H / DC bias → H2 even-order — priorité **HAUTE**

🎚️ **Hypothèse fréquemment évoquée** dans le discours technique grand public (Tape Op, Gearspace) pour le caractère Manley Massive Passive : un **biais asymétrique** (étage tube SE, cathode bias) pousse la boucle B-H hors symétrie centrale → composante H2 audible. Manley ne publie pas de décomposition mécanistique explicite — c'est une lecture d'ingénieurs/reviewers, pas une déclaration fabricant.

📚 **Mécanisme physique** : J-A scalaire classique symétrique par construction → un offset H_DC injecté dans `Heff = H + H_DC + α·M` brise la symétrie et produit du H2 (dérivable et mesurable au banc).

🧠 **À tester** : que cet offset H_DC dans le modèle reproduit qualitativement *et* quantitativement la signature H2 d'unités de référence (Manley, EAR 660). Mesure FFT multi-niveau requise.

Implémentation candidate, par ordre de simplicité/coût :
- (a) Offset H_DC paramétrique dans `Heff = H + H_DC + α·M` — **simple, recommandé pour spike initial**
- (b) Variante asymétrique J-A (Hauser/Sutor)
- (c) Preisach amélioré (Zhang 2023, MDPI Materials)

### 2.3 Hystérésis grande boucle vs petite — priorité MOYENNE

Les paramètres `c` (réversibilité de paroi) et `k` (pinning) gouvernent la "softness" du genou.

- **Mastering** : c élevé (0.3–0.5), k modéré → beaucoup de magnétisation réversible, peu de pertes par cycle, knee très progressif
- **Mic-pre Neve** : c plus bas (0.05–0.15), k plus haut → caractère plus "punchy", saturation plus définie

Hc ≈ k·(1−c) confirmé en littérature J-A.

### 2.4 Pertes excess Bertotti K2 — priorité MOYENNE-HAUTE

K2 modélise le **damping des parois de domaine** par micro-courants. Effet perceptuel : "smoothness" des transients HF, légère réduction d'attaque — ce que les ingés mastering appellent le côté "civilisé" d'un EAR 660 ou Neve 33609. Plus le noyau est en nickel haute perméabilité, plus K2 est élevé. Candidat fort pour différencier "mic-pre brutal" vs "mastering smooth".

### 2.5 Leakage L + interwinding capacitance — priorité MOYENNE

La résonance LC parasite (déjà modélisée) génère une **bosse HF "air"** typique 30–80 kHz. Sur mastering grade avec litz multi-couches (API 2503 : *"~±1°/10Hz to 100kHz"*), résonance très haute et amortie → quasi pas de bosse audible. Sur Marinair Neve 33609 elle peut tomber à 25–35 kHz et produire un "open top". À paramétrer par preset, **pas une signature mastering universelle**.

### 2.6 Pertes eddy K1 — priorité MOYENNE

K1 produit un rolloff doux HF. Pour les noyaux mastering en laminations très fines (0.05 mm nickel-iron), K1 est plus faible que pour silicon steel 0.35 mm → HF plus étendu, "ouvert". Différentiateur matériau important.

### 2.7 Permeabilité incrémentale Lm(B) variable — priorité **HAUTE**

📚 **Mécanisme physique** : à niveau croissant, µ_Δ chute → Lm chute → le pôle HP remonte → **compression fréquence-dépendante** entre 20 et 200 Hz. Dérivable directement du J-A.

🎚️ **Description courante** dans la littérature audio : c'est ce que les ingés appellent souvent "dynamic EQ" naturel.

🧠 **À tester** : que ce mécanisme contribue de façon dominante au "bloom" perçu (vs simple distorsion harmonique LF). Comparaison A/B "Lm fixe" vs "Lm(B) dynamique" requise.

Déjà implicitement présent dans le couplage J-A/Lm de TWISTERION mais à *exposer* explicitement dans le rendu audio.

### 2.8 Hysteresis-induced phase shift NL → 3D / front-back — priorité MOYENNE-HAUTE

📚 **Mécanisme physique** : l'hystérésis introduit un déphasage non-linéaire niveau- et fréquence-dépendant (mesurable au banc). Pour le capturer en phase, le solver J-A doit ne pas être trop "smoothed" (RK4 plutôt qu'Euler explicite).

🧠 **Lien causal "phase shift NL → image 3D / front-back"** : **hypothèse non démontrée**. Katz attribue le front-to-back principalement aux indices de réverbération précoce, pas à la phase NL transfo. Plausible mais à mesurer perceptuellement avant d'asserter une priorité élevée. **Reclasser HAUTE → MOYENNE-HAUTE** en attendant validation.

### 2.9 Niveau de fonctionnement (Bsat headroom) — priorité **HAUTE**

🎚️ **Constat fabricant** : Manley Vari-Mu spec à +30 dBu max et <0.1 % THD à 1 kHz → fonctionnement très en dessous du genou de saturation.

🧠 **Estimation typique non publiée** : `B_pic ≈ une faible fraction de Bsat` pour mastering grade (vs mic-pre où on monte plus près du genou sur peaks). **Chiffres précis à mesurer** sur unités de référence — l'estimation "5–20 % Bsat" qui circule dans le draft initial est une intuition, pas une mesure.

🧠 **Hypothèse de design** : la "depth" vient majoritairement du régime petit-signal hystérétique — non démontré, mais cohérent avec le headroom annoncé en mastering.

**Action plugin** : gain staging interne tel qu'au niveau preset par défaut, B_pic est nettement sous le genou. Calibration exacte à dériver de mesure (§6).

### 2.10 Topologie balanced/CT vs SE — priorité MOYENNE

📚 **Suppression idéale en théorie** : push-pull à center-tap supprime le H2 (et tous les harmoniques d'ordre pair) par symétrie de courant primaire. **En pratique** la suppression est partielle (asymétrie de bobinage, asymétrie de drive, biais résiduel) → H2 réduit mais non nul. Topologie SE (rare en mastering moderne sauf EAR 660 mu-follower) → conserve pleinement H2.

🧠 À distinguer de la signature **matériau** (§2.11) : topologie et matériau contribuent à des axes orthogonaux. Un PP balanced sur mu-metal vs PP balanced sur SiFe → seul le matériau diffère. Un SE sur mu-metal vs PP sur mu-metal → seule la topologie diffère.

À implémenter comme **flag par preset** indépendant du matériau.

### 2.11 Choix matériau noyau — priorité **HAUTE**

| Matériau | Bsat | µ_max | Hc | Signature |
|---|---|---|---|---|
| Mu-metal 80% Ni | ~0.7 T | 80–100k | très bas | "soft", grave épais, mid scoopé |
| Permalloy 50% Ni (1J85) | ~1.1 T | 30–50k | bas | équilibré, "hi-fi" |
| GO Silicon steel | ~1.8 T | 5–20k | plus haut | punchy, "iron" attack — SPL Iron, Carnhill |
| Hybride (Jensen) | mixte | mixte | très bas THD | quasi-linéaire |

Fichiers `data/materials/mu_metal_80ni.json`, `nife_50.json`, `permalloy_1j85_sun2023.json` déjà présents — base saine.

### 2.12 Couplage en cascade — priorité **HAUTE**

📚 **Mécanisme physique cohérent** : two transformer stages cascadés ne produisent pas une "depth" linéairement additive. Le premier transfo modifie le spectre vu par le second ; le H2 du premier devient une excitation pour le second, produisant des intermodulations dont la composante d'ordre pair est non triviale.

🧠 **Pas de mesure publiée à ma connaissance** quantifiant l'effet perceptuel d'un input+output cascadé vs un seul stage à gain équivalent. Hypothèse théorique cohérente, à mesurer (FFT multi-tons, comparaison directe).

Argument architectural pour une **chaîne plugin "Input transfo → process → Output transfo"** plutôt qu'un seul block — quel que soit le résultat de la mesure, l'architecture cascadée est plus modulable.

---

## 3. Inventaire transfos / circuits mastering iconiques

> **Disclaimer** : les colonnes "matériau / Bsat / Lm estimés" 🧠 sont des **inférences** à partir de datasheets partielles, équivalents publics et discussions forums. Aucune n'est confirmée fabricant. Les "signatures subjectives" 🎚️ proviennent de reviews et orientent le caractère visé — elles ne prouvent pas le mécanisme physique sous-jacent. Manley/RND/Marinair ne publient pas de décomposition mécanistique.

| Unité | Transfo(s) | Matériau (estimé) | Ratio | Bsat est. | Lm est. | Signature subjective |
|---|---|---|---|---|---|---|
| **Manley Massive Passive** | Manley custom in/out, nickel laminations en boîtier mu-metal | 50% NiFe | ~1:1 | ~1.1 T | 20–40 H | "Great depth", "girth", "organic vibe" (SOS, Tape Op) |
| **Manley Vari-Mu** | Custom hand-wound, nickel | 50% NiFe | 1:1 (600 Ω) | ~1.1 T | 30 H | "+30 dBu max, 26 dB headroom, <0.1% THD" — "smooth, transparent, mastering standard" |
| **Neve 33609/N** | Marinair input, bridge-driver, sidechain, output | GO SiFe | divers | ~1.5 T | 5–15 H | "Glue", "smooth to overdriven hard-clipping" |
| **SPL Iron** | Lundahl custom, mu-metal shielded | mu-metal 80% | mixte | ~0.7 T | 30+ H | "Pleasant, melodic, transparent compression" |
| **RND Portico II MBP** | RND custom in/out, 72V topology | hybride | ~1:1 | n.c. | n.c. | "Silk" (réduit feedback → +H2), "Silk+", Stereo Field Editor |
| **Crane Song HEDD** | Émulation DSP | — | — | — | — | Triode = +H2 fattening LF; Pentode = HF; Tape = H3/H5 |
| **API 2500** | API 2503 output (litz quad-filar 75:75 Ω) | GO SiFe | 1:1 | ~1.5 T | modérée | Bandwidth MHz, ±1°/10Hz–100kHz |
| **EAR 660** | Tim de Paravicini hand-wound, "giant" | nickel haute µ | divers | ~0.8 T | 40+ H | "Smooth, warm, clarity" — mu-follower → H2 prononcé |

**Note datasheets** : courbes B-H exactes quasi-jamais publiées (Manley, RND, Marinair propriétaires). Sources pratiques :
- Datasheets Lundahl publiques (LL1517, LL1538) → proxy SPL Iron/Manley
- Sowter 8665, 9542
- Mesures bench publiées (Ethan Winer, Owen Curtin)
- AMS Neve / Carnhill courbes B-H Marinair (parfois sous NDA)

---

## 4. Stratégie de modélisation dans le codebase

### 4.1 Paramètres J-A "mastering grade" vs "mic-pre"

| Paramètre | Mic-pre (JT-115K-E) | Mastering (Manley MP-style) |
|---|---|---|
| Ms | ~1.4e6 A/m (silicon steel) | ~6e5 A/m (mu-metal 80%) |
| a | 30–80 A/m | **5–20 A/m** (genou doux) |
| k | 30–60 A/m | **3–10 A/m** (Hc bas) |
| c | 0.1–0.2 | **0.3–0.5** (forte réversibilité) |
| α | 1e-3 à 1e-2 | 5e-4 à 2e-3 (faible interaction) |

Ratio **Ms/a** = "raideur" anhystérétique : mastering ≥ 5e4, mic-pre 1–2e4. Ratio a/k ≈ 1–3 typique.

### 4.2 Faut-il étendre le modèle ?

🧠 **Hypothèse de travail** : J-A scalaire + Bertotti + Lm(B) + LC actuels devraient capturer une **part significative** du résultat audible (le pourcentage exact n'est pas asserté — à valider par A/B perceptuel, voir §6). Les ajouts à tester par ordre de ROI estimé :

1. **`H_DC` offset** dans le moteur J-A → capture l'asymétrie pour H2. Trivial à implémenter.
2. **Exposer Lm(B) dynamique** en sortie audio → candidat pour le "bloom".
3. **Cascading explicite** de deux instances → "input transfo" + "output transfo" séparés.
4. **K2 paramétrable séparément** de K1 → différencier matériaux nickel vs SiFe.

L'ordre de priorité ci-dessus est une intuition design ; les A/B blind du §6 doivent confirmer la hiérarchie.

**Ce qui demanderait du nouveau code (à éviter en sprint immédiat)** :
- Vector hysteresis Mayergoyz/Preisach 2D : utile uniquement transfos toroïdaux très bas niveau, marginal pour mastering
- Preisach asymétrique full (Zhang 2023) : 4× plus coûteux CPU vs J-A + H_DC offset
- Magnétostriction acoustique : effet réel mais 60+ dB sous le signal, non audible

### 4.3 Risques modélisation J-A scalaire

- Pas de capture de l'effet de proximité entre couches → bandwidth HF sous-estimée potentiellement
- Pas de magnétostriction → micro-bruit mécanique manquant (inaudible)
- Single-valued anhystérétique → si transfo a vraiment plusieurs domaines magnétiquement isolés (toroïdes nickel), J-A peut rater ~10 % de la magnétisation
- **TMT / mojo** (capacités parasites résiduelles, bobinage main-wound vs machine, vieillissement chimique des laminations) — **non modélisable**, à laisser au marketing

---

## 5. Distinction matériau vs topologie (axes orthogonaux)

À ne pas confondre dans les presets :

| Axe | Paramètres modèle | Effet primaire | Variation per preset |
|---|---|---|---|
| **Matériau noyau** | Ms, a, k, c, α, K1, K2 | Forme de la boucle B-H : raideur, Hc, réversibilité, pertes | Mu-metal vs NiFe-50 vs SiFe vs hybride |
| **Topologie** | Flag SE / PP-balanced / CT, présence/absence H_DC, présence/absence symétrie de drive | Suppression idéale (théorique) ou résiduelle (pratique) des harmoniques pairs ; couplage H_DC fabricant | SE → conserve H2 ; PP-balanced → suppression idéale H2, résiduelle en pratique |

Un preset complet = **matériau × topologie × niveau opérationnel**. Trois axes à tester séparément en A/B pour isoler les contributions.

---

## 6. Protocole de validation (à exécuter avant et pendant les sprints)

Cette section est **load-bearing** : elle conditionne la validité des claims du §2. Sans validation, le doc reste "hypothèses prioritaires structurées", pas démonstration.

### 6.1 Mesures bench (sur unités de référence ou modèle calibré)

| # | Mesure | Méthode | Sortie | Bloque quel claim |
|---|---|---|---|---|
| M1 | **Lm incrémentale vs niveau** | Sweep sinus 50 Hz, niveaux -40 → +24 dBu, mesure module impédance primaire ou flux/courant ; tracer Lm(B_pic) | Courbe Lm(B), confirme/infirme la chute µ_Δ | §2.1, §2.7, §4.2 item 2 |
| M2 | **THD/IMD vs flux estimé** | Sweep multi-niveau (-40 → +24 dBu) à 20 Hz, 100 Hz, 1 kHz, 10 kHz ; FFT 64k | THD(level, freq), IMD ratio H2/H3 | §2.2, §2.3, §2.9 |
| M3 | **Phase NL vs niveau** | Sinus 100 Hz petit signal vs gros signal, mesure phase relative (cross-correlation) ; idem 1 kHz, 5 kHz | Δφ(level, freq), confirme/infirme phase shift NL audible | §2.8 |
| M4 | **B_pic réel à niveau nominal** | Intégration tension secondaire, comparaison avec Bsat estimé | Ratio B_pic/Bsat à +4 dBu nominal et +18 dBu peak | §2.9 (chiffre exact, remplace l'estimation 5–20 %) |
| M5 | **H2/H3 vs symétrie de drive** | Drive avec/sans biais DC ajouté, FFT, ratio H2(drive_asym) / H2(drive_sym) | Confirme effet H_DC sur le modèle | §2.2, §2.10 |

Cible mesure réelle : prêt d'unité (Manley Vari-Mu, EAR 660, RND MBP) ou proxy datasheet (Lundahl LL1538 publique).

### 6.2 A/B perceptuel blind

Pour chaque ajout (`H_DC`, Lm exposé, cascade), faire :

1. **Pair de bounces** identique sauf le mécanisme testé activé/désactivé
2. **Matériel test** : 5 sources mastering-typical (mix complet rock, pop dense, jazz acoustique, classique orchestral, voix solo), 30 s chacun
3. **Test ABX** : sujet (Michael + 2 ingés mastering si possible), N=20 trials, p<0.05 binomial pour considérer l'effet audible
4. **Questionnaire qualitatif** : "depth", "bloom", "image 3D", "front-back" sur échelle 1–5

🧠 **Critère go/no-go** : un mécanisme qui ne passe pas l'ABX (p>0.05) doit être **déprionisé**, pas implémenté en preset.

### 6.3 Validation modèle vs unité de référence

Une fois H_DC + cascade implémentés :

1. Identifier H_DC, K2, Lm de référence par CMA-ES sur datasheet de l'unité (cf. pipeline `identification/`)
2. Mesurer THD/IMD du modèle aux mêmes points que M2 sur l'unité
3. Erreur cible : |Δ| ≤ 3 dB sur 80 % des points (cf. critère Sprint 1 calibration Jensen)
4. Si l'erreur excède la cible, le mécanisme manque dans le modèle (suspect candidat : non-linéarité de Lm(B) plus complexe que ce que J-A capture, ou TMT/aging non modélisable)

### 6.4 Gate avant production

Aucun preset mastering n'entre en factory preset (`Presets.h::kFactoryCount`) tant que :

- M1, M2 passées sur l'unité ou proxy
- ABX de §6.2 montre p<0.05 pour au moins un mécanisme
- Erreur §6.3 dans la cible

Tant qu'on ne passe pas ces gates, les presets restent en **user presets JSON expérimentaux** dans `data/transformers/mastering_*.json`.

---

## 7. Tableau récapitulatif — mécanisme × paramètre × priorité × preuve

Priorités révisées après pondération par degré de preuve :

| Mécanisme | Impact perceptuel hypothétique | Paramètre modèle | Preuve actuelle | Priorité |
|---|---|---|---|---|
| Lm grande, load-dependent | Bloom LF, weight, phase shift bas | Lm(B), couplage J-A | 📚 mécanisme + 🧠 lien percept. | **HAUTE** |
| Asymétrie B-H / DC bias | H2 even, "warmth", 3D | À ajouter : `H_DC` offset | 📚 mécanisme + 🎚️ usage + 🧠 dominance | **HAUTE** |
| µ_Δ incrémentale variable | LF dynamic EQ, breathing | Déjà émergent J-A | 📚 mécanisme + 🧠 lien "bloom" | **HAUTE** |
| Choix matériau (Ni/SiFe) | Signature globale | Ms, a, k, c par matériau | 📚 mesure publique différentes familles | **HAUTE** |
| Cascading input+output | Renforcement H2 par cross-mod | Architecture chaîne plugin | 📚 mécanisme + 🧠 quantification | **HAUTE** |
| Niveau de fonctionnement | Hystérésis active sans sat. | Gain staging / Bsat ratio | 🎚️ headroom fabricant + 🧠 lien percept. | **HAUTE** |
| Bertotti K2 (excess) | Smoothness transients | K2 paramétrable | 📚 mécanisme + 🧠 percept. | MOYENNE-HAUTE |
| Hystérésis phase shift NL | Front-back (incertain) | Solver ODE précis | 📚 mesurable + 🧠 lien percept. *non démontré* | MOYENNE-HAUTE *(reclassé)* |
| Hystérésis loop large/petite | Soft vs hard knee | c, k Jiles | 📚 mécanisme | MOYENNE |
| Bertotti K1 (eddy) | HF rolloff doux | K1 par matériau | 📚 mécanisme | MOYENNE |
| Leakage + Cw resonance | "Air" HF bump | Déjà LC parasitique | 📚 mécanisme + 🎚️ confirmé | MOYENNE |
| Topologie SE/PP/CT | Suppression idéale H2 (partielle en pratique) | Flag à ajouter | 📚 théorie symétrie | MOYENNE |
| Preisach asymétrique full | Marginal vs J-A + H_DC | — | 📚 alternative coûteuse | BASSE |
| Vector hysteresis 2D | Toroïdes bas niveau | — | 📚 marginal pour mastering | BASSE |
| Magnétostriction | Inaudible | — | 📚 sub-audible | NULLE |
| TMT / mojo / aging | Marketing | — | 🎚️ non modélisable | NULLE |

---

## Sources

### Mastering grade hardware
- [Manley Massive Passive review — SafeandSound Mastering](https://www.masteringmastering.co.uk/manley-massive-passive-review.html)
- [Manley Variable MU Mastering — Manley Labs](https://www.manley.com/products/pro-audio/dynamics/variable-mu)
- [Manley Vari-Mu alternative transformers — GroupDIY](https://groupdiy.com/threads/manley-vari-mu-alternative-transformers.50065/)
- [AMS Neve 33609 Stereo Compressor](https://www.ams-neve.com/outboard/limiter-compressors/33609-stereo-compressor/)
- [SPL IRON Mastering Compressor — Tape Op](https://tapeop.com/reviews/gear/114/iron-mastering-compressor)
- [Portico II Master Buss Processor — Tape Op](https://tapeop.com/reviews/gear/89/portico-ii-master-buss-processor)
- [Crane Song HEDD 192 — Tape Op](https://tapeop.com/reviews/gear/26/hedd-192)
- [Crane Song HEDD pentode/triode page](http://www.cranesong.com/products/hedd/manual/pentandtriode.html)
- [API 2503 transformer — GroupDIY](https://groupdiy.com/threads/api-2503-transformer.72256/)
- [API 2500 — Sound on Sound review](https://www.soundonsound.com/reviews/api-2500)
- [EAR 660 — Tim de Paravicini, recordproduction.com](https://www.recordproduction.com/reviews/ear-yoshino-660)

### Théorie audio transfo
- [Audio Transformers — Bill Whitlock / Jensen (PDF)](https://www.jensen-transformers.com/wp-content/uploads/2014/08/Audio-Transformers-Chapter.pdf)
- [Transformers For Small Signal Audio — sound-au.com](https://sound-au.com/articles/audio-xfmrs.htm)
- [Output transformer level dependent low frequency response — diyAudio](https://www.diyaudio.com/community/threads/output-transformer-level-dependent-low-frequency-response.382983/)

### Psychoacoustique / mastering
- [Bob Katz — Mastering Audio (Routledge)](https://www.routledge.com/Mastering-Audio-The-Art-and-the-Science/Katz/p/book/9780240818962)
- [Katz's Corner Episode 25: Adventures in Distortion — Stereophile](https://www.stereophile.com/content/katzs-corner-episode-25-adventures-distortion)
- [Sage Audio — Saturation Masterclass](https://www.sageaudio.com/blog/mastering/saturation-masterclass)
- [Sonarworks — When Distortion Is Good](https://www.sonarworks.com/blog/learn/when-distortion-is-good)

### Modèles hystérésis (asymétrie / DC bias)
- [Jiles–Atherton model — Wikipedia](https://en.wikipedia.org/wiki/Jiles%E2%80%93Atherton_model)
- [Harmonic and DC Bias Hysteresis Simulation — MDPI Materials 2023](https://www.mdpi.com/1996-1944/16/12/4385)
- [Comparative study of J–A and Preisach for core loss under DC bias — Sci. Rep. 2024](https://www.nature.com/articles/s41598-024-55155-w)
- [Numerical determination of J–A parameters — ResearchGate](https://www.researchgate.net/publication/235263208_Numerical_determination_of_Jiles-Atherton_model_parameters)

### Transfos publics (proxy)
- [Ultimate Audio Transformer List — DIY Recording Equipment](https://www.diyrecordingequipment.com/blogs/news/15851752-work-in-progress-the-ultimate-audio-transformer-list)
- [Lundahl Transformers Catalogue 2020 (PDF)](https://www.lundahltransformers.com/wp-content/uploads/catalogue/Catalogue_20200514.pdf)
