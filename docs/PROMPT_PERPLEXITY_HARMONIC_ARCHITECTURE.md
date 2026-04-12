Je modelise un preampli discret JE-990 (Deane Jensen, 1980) en Wave Digital Filters temps reel (C++17, plugin VST3/AAX). Le modele produit H2 > H3 dans la chaine complete alors que le vrai circuit produit H3 > H2 (signature push-pull). J'ai tente une suppression analytique du H2 dans le VAS — c'est un pansement qui ne resout pas le probleme structurel. Je cherche comment l'architecture WDF elle-meme devrait etre construite pour produire nativement les bonnes harmoniques.

Cherche dans les publications de Kurt James Werner (Stanford/McGill), Stefano D'Angelo (Aalto/Native Instruments), Julius O. Smith III (Stanford CCRMA), Maarten van Walstijn (Queen's Belfast), Alberto Bernardini & Augusto Sarti (Polimi), Rafael de la Fuente, Martin Holters, les proceedings DAFx, AES, IEEE Signal Processing, SMAC, les theses sur virtual analog / WDF / circuit emulation.

---

# Le circuit reel JE-990

```
Entree → DiffPair (LM-394 NPN matched) → Cascode (Q3/Q5)
       → VAS Q6 (2N4250A PNP common-emitter, R_coll=60k, R_emitter=160Ω, C_miller=150pF)
       → ClassAB output (Q8 MJE-181 NPN + Q9 MJE-171 PNP, push-pull emitter follower)
       → LoadIsolator (39Ω + 40µH)
       → Sortie
       ↓ feedback
       beta = Rg/(Rfb+Rg) = 47/(1430+47) ≈ 0.032
       Loop gain reel ≈ 125 dB (tres eleve → linearise le circuit, supprime H2 du VAS)
```

Signatures harmoniques du vrai circuit :
- **DiffPair** : differentiel, annule H2 par symetrie
- **Cascode** : lineaire (buffer de courant), pas d'harmoniques
- **VAS single-ended CE** : genere H2 (asymetrie diode exp(Vbe/Vt)), MAIS le loop gain de 125 dB le supprime dans le vrai circuit
- **ClassAB push-pull** : genere H3 (crossover odd-harmonic), annule H2 par symetrie complementaire NPN/PNP
- **Resultat reel** : H3 > H2 (signature push-pull odd-harmonic dominante)

---

# Mon implementation WDF et ses 4 problemes structurels

## Probleme 1 : Le ClassAB utilise un "Ic-weighted push-pull" qui agit comme un rectificateur

Au lieu d'une combinaison complementaire standard (Ve_q8 + Ve_q9)/2, j'utilise une ponderation par le courant collecteur :

```cpp
// ClassABOutputWDF.h — Push-pull combination
const float Ic_abs_q8 = std::abs(lastIc_q8_);
const float Ic_abs_q9 = std::abs(lastIc_q9_);
const float Ic_total = Ic_abs_q8 + Ic_abs_q9;

float Ve_combined;
if (Ic_total > kEpsilonF)
{
    const float f8 = Ic_abs_q8 / Ic_total;  // poids Q8
    const float f9 = Ic_abs_q9 / Ic_total;  // poids Q9
    Ve_combined = f8 * Ve_q8 + f9 * Ve_q9;  // moyenne ponderee
}
else
{
    Ve_combined = (Ve_q8 + Ve_q9) * 0.5f;
}
```

**En isolation** (open-loop) : la transition sharp crossover genere H3 >> H2 → **correct**.

**Dans la boucle fermee** : la ponderation `f8 = |Ic_q8|/(|Ic_q8|+|Ic_q9|)` agit comme une **rectification douce**. Quand le signal va en positif, f8→1, f9→0 (Q8 seul). Quand il va en negatif, f9→1, f8→0 (Q9 seul). Cette fonction de ponderation est elle-meme **non-lineaire et non-symetrique** par rapport au signal. La fonction de transfert `|Ic|/(|Ic_q8|+|Ic_q9|)` contient une composante paire (a cause du |abs|) qui genere H2, H4, H6. En isolation, le H3 de la transition crossover domine. Mais dans la boucle, le feedback **supprime preferentiellement H3** (puisque le Jacobien est calcule a partir de gains lineaires) et **laisse passer H2** du rectificateur.

Q8 et Q9 sont chacun modele comme un **WDF one-port BJTLeaf** independant. Chacun resout son propre NR (Ebers-Moll companion source) pour trouver Vbe. Puis on extrait Ic = Bf * Ib, Ve = Vdrive - Vbe. La "combinaison push-pull" est faite APRES les deux NR, comme un post-traitement.

**Question fondamentale** : dans la litterature WDF, comment modelise-t-on un etage push-pull (deux BJT complementaires partageant un noeud emetteur commun et alimentant une charge commune) ? Existe-t-il un adaptateur WDF multi-port ou un R-type adaptor (Bernardini & Sarti 2015) qui capture nativement la symetrie push-pull et l'annulation H2 ? Ou bien la combinaison de deux one-ports independants est-elle fondamentalement incapable de reproduire la symetrie ?

## Probleme 2 : Le DiffPair applique la contrainte de courant de queue APRES les NR independants

```cpp
// DiffPairWDF.h — Tail current constraint
// Q1 et Q2 resolus independamment par WDF scatter + NR
q1Leaf_.scatter(a_q1);
q2Leaf_.scatter(a_q2);
float Ic1_raw = q1Leaf_.getIc();
float Ic2_raw = q2Leaf_.getIc();

// Post-hoc scaling to enforce I_tail constraint
const double Ic_sum = std::abs(Ic1_raw) + std::abs(Ic2_raw);
if (Ic_sum > kEpsilonF)
{
    const double scale = I_tail / Ic_sum;
    Ic1_ = Ic1_raw * scale;
    Ic2_ = Ic2_raw * scale;
}

// Differential output
float Vout = (Ic1_ - Ic2_) * R_load;
```

Un vrai differential pair a un couplage intrinseque : si Ic1 augmente, Ic2 DOIT diminuer (conservation I_tail). La loi exacte est `Ic1 = I_tail * 1/(1 + exp(-deltaVbe/Vt))`. Ce couplage cree la symetrie differentielle qui annule H2.

Mon implementation resout Q1 et Q2 comme deux BJT independants, puis force `Ic1+Ic2 = I_tail` par scaling lineaire. Ce scaling est lui-meme une non-linearite supplementaire (dependante du signal) qui brise la symetrie differentielle et injecte du H2.

**Question** : la litterature WDF traite-t-elle le cas d'un differential pair avec source de courant partagee ? Y a-t-il une formulation WDF a 3 ports (Q1 + Q2 + I_tail) avec un solveur implicite unique qui capture le couplage naturel ? Ou bien la decomposition en 2 one-ports + scaling post-hoc est-elle la seule approche viable en WDF ?

## Probleme 3 : Le Newton solver de la boucle feedback utilise un probe LINEAIRE et un commit NON-LINEAIRE

```cpp
// JE990Path.h — Newton solver
// PROBE (pour calculer g0):
const float v3 = vas_.processSample(v2);               // VAS: nonlineaire (BJT + tanh)
const double y0 = double(v3) * classAB_.getLocalGain(); // ClassAB: LINEAIRE (gain scalar)
const double g0 = y - y0;
y = y - g0 / J;  // J = 1 + beta * Aol (analytical, smooth)

// COMMIT (avec le vrai feedback):
const float c3 = vas_.processSample(c2);
classAB_.processSample(c3);  // ClassAB: NONLINEAIRE (Ic-weighted dual BJT NR)
```

Le Jacobien `J` et la probe `g0` sont calcules avec un ClassAB **lineaire** (getLocalGain = gm_total/(gm_total + g_sense)). Mais le commit utilise le ClassAB **nonlineaire** (Ic-weighted push-pull + dual BJT NR). Le feedback loop "voit" une nonlinearite differente de celle qu'il essaie de corriger.

Consequence : le feedback supprime H3 (qu'il ne modele pas dans la probe) et laisse passer H2 (que le VAS genere mais que la probe ne detecte pas comme un probleme).

**Question** : dans les implementations WDF avec delay-free loops (Werner 2016, Bernardini & Sarti), comment gere-t-on un etage nonlineaire dans la boucle de feedback quand le solveur implicite ne peut pas evaluer l'etage complet (a cause de l'anomalie WDF one-port EF) ? Existe-t-il une formulation du Jacobien qui tient compte de la nonlinearite du ClassAB sans l'evaluer ? Ou bien un solveur a 2 equations couplees (VAS + ClassAB) ?

## Probleme 4 : Le loop gain WDF (~36 dB effectif) est 90 dB en dessous du reel (~125 dB)

Le vrai JE-990 a un loop gain suffisant pour supprimer le H2 du VAS de ~60 dB. Notre loop gain :
```
Aol = Av_dp * Av_cas * Av_vas * Av_ab = 6.3 * 0.98 * 338 * 0.9 ≈ 1880 (65 dB open-loop)
Loop gain = beta * Aol = 0.032 * 1880 ≈ 60 (36 dB closed-loop)
```

Le Aol theorique du vrai circuit est ~4 million (132 dB) grace au current mirror Q7 qui donne un R_coll effectif de ~5 MΩ (pas nos 60 kΩ). Avec beta=0.032, le loop gain reel = 0.032 * 4M = 128k (102 dB), supprimant le H2 du VAS de >80 dB.

Notre R_coll_AC=60kΩ est un compromis : le vrai current mirror n'est pas modelisable comme une resistance simple. Augmenter R_coll augmenterait Aol mais rendrait le Newton solver instable (J trop grand → pas de Newton trop petit → lenteur/oscillation).

**Question** : comment les emulateurs commerciaux (UAD, Arturia, Brainworx) modelisent-ils l'impedance de sortie d'un current mirror dans un VAS WDF ? Utilisent-ils un R_coll_AC eleve avec un solveur plus robuste ? Ou bien un gain supplementaire "virtuel" qui n'est pas modele comme une resistance physique ? Y a-t-il une technique pour augmenter le loop gain effectif dans le domaine onde sans casser la passivite WDF ?

---

# Ce que je NE veux PAS comme solution

- **Suppression analytique post-hoc du H2** (deja essaye, c'est un pansement, eta*Ic_even ne peut pas compenser un defaut structurel)
- **Boost artificiel du H3** avant ou apres la boucle feedback (masque le probleme)
- **Modification du seuil du test** (le test est correct, c'est le modele qui doit changer)
- **Filtre selectif dans la boucle feedback** pour supprimer H2 (pas physiquement justifie)

# Ce que je cherche

1. **Comment modeliser un etage push-pull Class-AB en WDF** pour qu'il produise nativement H3 > H2 dans une boucle fermee, pas seulement en isolation. Y a-t-il un adapteur WDF (R-type, multi-port) qui capture la symetrie complementaire NPN/PNP sans passer par deux one-ports independants + combinaison post-hoc ?

2. **Comment modeliser une paire differentielle en WDF** avec le couplage intrinseque du courant de queue (I_tail conservation) integre dans le solveur WDF, pas comme un scaling post-hoc ?

3. **Comment formuler un solveur implicite (Newton delay-free loop)** qui tient compte de la nonlinearite de TOUS les etages dans la boucle, y compris le ClassAB, sans evaluer physiquement chaque etage (ce qui causerait l'anomalie WDF one-port EF) ?

4. **Comment augmenter le loop gain effectif** d'un amplificateur WDF a feedback pour qu'il approche le vrai ~125 dB, sans casser la passivite, la stabilite du solveur, ou le budget CPU ?

5. **Precedents dans la litterature ou l'industrie** : quelqu'un a-t-il deja modele un op-amp discret complet (type 990, 2520, Forssell 500) en WDF avec les bonnes signatures harmoniques dans la boucle fermee ? Si oui, quelle architecture WDF ont-ils utilisee ?

# Donnees numeriques de reference

```
VAS standalone:     gain=338.2, gm=58mA/V, Vt=26mV, Ic_q=1.5mA
ClassAB isole:      H2/H3 = 0.018 (H3 >> H2, CORRECT)
Full chain JE-990:  H2 = -70.05 dB, H3 = -79.11 dB (H2 dominant, INCORRECT)
Feedback beta:      0.032
Loop gain effectif: ~36 dB (WDF) vs ~102 dB (reel)
Budget CPU:         <5% d'un coeur a 96kHz, buffer 512
```

Cite les sources avec DOI ou lien quand possible. Donne du pseudo-code C++ pour les solutions proposees.
