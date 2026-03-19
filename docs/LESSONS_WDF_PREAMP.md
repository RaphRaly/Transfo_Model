# Lecons Apprises — Modelisation WDF Preamp (Sprint 3)

**Date**: 2026-03-19
**Contexte**: Sprint 3 (Neve Heritage Class-A, 3 transistors WDF companion-source)
**Fichiers concernes**: `CEStageWDF.h`, `EFStageWDF.h`, `NeveClassAPath.h`

---

## 1. Polarisation DC obligatoire pour les BJT WDF

### Erreur
Les etages CE/EF n'avaient aucune source de polarisation DC.
Pendant le settling (`prepare()` avec input=0V), la tension de base etait 0V
→ Vbe ≈ 0V → BJT coupe → Ic = 0, gm = 0, aucun gain.

### Symptomes
- Gain open-loop ≈ 0 dB pendant le settling
- Zout de l'etage EF = 1e6 Ohm (au lieu de ~11 Ohm)
- Le systeme ne converge jamais vers un point de fonctionnement stable

### Regle
**Chaque etage WDF companion-source DOIT calculer automatiquement une tension
de polarisation `V_bias_base_` dans `prepare()`** et l'ajouter au `baseDrive`
dans `processSample()`.

```cpp
// Calcul auto de V_bias dans prepare() :
const float Ic_target = Vcc / (2.0f * R_collector);  // Collecteur a mi-tension
V_bias_base_ = sign_ * Vt * std::log(Ic_target / Is + 1.0f);

// Application dans processSample() :
baseDrive += V_bias_base_;
```

### Pourquoi ca marche
En WDF, a l'etat stationnaire, `Vbe → baseDrive`. Donc ajouter V_bias (= Vbe
cible au courant de repos) au baseDrive polarise naturellement la jonction
BE au bon point de fonctionnement.

---

## 2. Feedback explicite avec delai 1-echantillon → instable

### Erreur
La boucle de feedback etait implementee comme :
```cpp
inputWithFb = input - beta * outputPrev_;  // outputPrev_ = sortie echantillon precedent
```
Avec un gain en boucle ouverte Aol ≈ 100 dB (160'000 en lineaire), le gain
de boucle T = Aol * beta >> 1. Le pole du systeme est a z = -T, loin en
dehors du cercle unite → **oscillation systematique** (sortie alterne entre
+24V et -24V a chaque echantillon).

### Regle
**Ne JAMAIS utiliser de feedback explicite avec delai d'un echantillon quand le
gain de boucle T > 1.** C'est le cas pour tout ampli a haute boucle ouverte.

### Solution
Pour un amplificateur avec Aol >> Acl (ce qui est le cas normal en
electronique), utiliser le **gain analytique** :
```cpp
Acl = 1.0f + Rfb_ / Rg_;
output = v3_stages * (Acl / Aol);
```
C'est physiquement correct : quand Aol >> Acl, le gain boucle fermee est
determine **uniquement par le reseau passif de feedback** (Rfb et Rg),
independamment du gain des etages actifs.

### Alternatives (non retenues)
- **Feedback zero-delai iteratif** : correct mais necessite rollback des etats
  WDF reactifs (condensateurs, filtres DC) entre iterations — trop complexe.
- **Filtre LP sur le feedback** : reduirait le gain de boucle a Nyquist mais
  altererait la reponse en frequence du gain boucle fermee.
- **Reduction du gain OL par Miller aggressif** : fc devrait etre < 1 kHz pour
  stabiliser a position 0 (beta=0.32), ce qui detruirait la bande passante.

---

## 3. Degeneration emetteur manquante dans le chemin signal

### Erreur
`R_emitter` (Rg=47 Ohm pour Q1, Re=7.5k pour Q2) etait un parametre de
config utilise UNIQUEMENT dans `getGainInstantaneous()` (monitoring) et
le bypass cap. Le `processSample()` n'appliquait aucune degeneration,
donnant un gain reel de -gm*Rc ≈ -450 au lieu de -Rc/(Re+1/gm) ≈ -200.

### Symptomes
- Le rapport `Acl/Aol` pour la correction de gain etait faux
  (Aol estime = 170, Aol reel = 200'000)
- Le signal saturait les rails d'alimentation meme pour des entrees de 1 mV

### Regle
**Si un composant affecte le gain dans le circuit reel, il DOIT affecter le
gain dans `processSample()`.** La degeneration emetteur se modele comme un
facteur de mise a l'echelle sur la composante AC de Vc :

```cpp
// Step 5b: Degeneration emetteur
if (R_emitter > 0 && !hasBypassCap_) {
    const float degen = 1.0f / (1.0f + gm * R_emitter);
    Vc = Vc_dc_ + (Vc - Vc_dc_) * degen;
}
```

---

## 4. Transitoire initial apres `reset()` amplifie par le gain OL

### Erreur
Apres `reset()`, le BJTLeaf a `b_reflected_ = 0` et `Vbe_prev = ±0.6V`.
Le premier echantillon produit `a = 2*baseDrive - 0 = 2*V_bias`, ce qui
est le DOUBLE de la tension Thevenin voulue. Le NR converge a un Vbe
surevalue → Ic enorme → Vc au rail → amplifie par l'etage suivant →
sortie a ±24V.

### Regle
**Apres `reset()`, faire un "warmup" de ~32 echantillons a entree nulle**
pour laisser le solveur NR converger au point de repos. Puis reinitialiser
les traqueurs DC a la valeur convergee :

```cpp
void reset() {
    bjtLeaf_.reset();
    // ... reinit DC ...
    for (int i = 0; i < 32; ++i) processSample(0.0f);
    Vc_dc_ = Vc_last_;       // Recaler le traqueur DC
    millerState_ = Vc_last_;  // Recaler le filtre Miller
    outputVoltage_ = 0.0f;
    dcSettleCount_ = 0;
}
```

---

## 5. Initialisation des etats DC/filtres au point de repos

### Erreur
- `Vc_dc_` initialise a une estimation (Ic_q = 1mA hardcode) differente du
  Ic reel au point de repos (0.8 mA) → ecart de 1.2V qui est amplifie
- `millerState_` initialise a 0 au lieu de Vc_quiescent ≈ 12V → transitoire
  de 12V au premier echantillon

### Regle
**Tous les etats de filtres et traqueurs DC DOIVENT etre initialises a la
valeur quiescente correspondant a la polarisation calculee** :

```cpp
// Dans prepare() :
Vc_quiescent_ = sign * Vcc - sign * Ic_target * R_collector;
Vc_dc_ = Vc_quiescent_;
millerState_ = Vc_quiescent_;

// Dans reset() : memes valeurs
```

Ce `Vc_quiescent_` doit etre calcule a partir du MEME `Ic_target` utilise
pour `V_bias_base_`, sinon il y a un mismatch qui genere un transitoire.

---

## 6. Double scatter du condensateur de couplage

### Erreur
Le code avait deux chemins pour le condensateur de couplage :
1. Scatter simple (lignes 211-225) qui mettait a jour `state_`
2. Scatter via WDFSeriesAdaptor (lignes 255-281) qui re-scatterait le meme cap

Le condensateur WDF est un element a memoire (`state_ = a[n-1]`).
Le double scatter ecrasait le premier etat, introduisant une erreur
d'un echantillon dans la reponse du filtre passe-haut.

### Regle
**Chaque element WDF reactif ne doit etre scattere qu'UNE SEULE FOIS par
echantillon.** Si on simplifie l'arbre WDF (ex: supprimer le series
adaptor), il faut aussi supprimer le code qui faisait le deuxieme scatter.

---

## 7. Couplage inter-etages : AC vs DC

### Erreur initiale envisagee
Tenter un couplage DC complet entre etages (Q1 → Q2 → Q3) semblait correct
physiquement. Mais dans le companion-source WDF, le port NR resout la
jonction BE : si on envoie un Vc = +9V (sortie Q1) directement a l'entree
de Q2 PNP, le solveur NR recoit une tension Thevenin de +9V a travers
Z=rbe ≈ 10kΩ. Le NR converge vers un Vbe profondement en inverse pour le
PNP (la jonction ne "voit" pas le reseau emetteur qui fixerait le courant
dans un vrai circuit).

### Regle
**Dans l'approche companion-source WDF, les etages doivent etre couples en
AC, chacun avec sa propre polarisation independante.** Chaque etage :
1. Recoit le signal AC de l'etage precedent
2. Ajoute sa propre V_bias pour polariser le BJT
3. Soustrait son propre Vc_dc pour sortir en AC

Le couplage DC ne fonctionne que si le reseau emetteur complet (Re vers
l'alimentation) fait partie de l'arbre WDF du port BE, ce qui est trop
complexe pour l'approche companion-source.

---

## Resume des regles

| # | Regle | Impact si ignoree |
|---|-------|-------------------|
| 1 | V_bias obligatoire dans chaque etage WDF | BJT coupe, gm=0, aucun gain |
| 2 | Pas de feedback explicite 1-echantillon avec Aol > 1/beta | Oscillation rail-a-rail |
| 3 | Degeneration emetteur dans processSample() | Gain reel ≠ gain estime → ratio faux |
| 4 | Warmup apres reset() (32 echantillons) | Transitoire ±24V au premier echantillon |
| 5 | Initialiser filtres/DC au point de repos calcule | Transitoire proportionnel au mismatch |
| 6 | Un seul scatter par element reactif par echantillon | Erreur de phase/amplitude du filtre |
| 7 | Couplage AC inter-etages avec bias independante | NR diverge en inter-etage DC |

---

## Application aux futurs sprints

### Sprint 4 (JE-990)
Les memes regles s'appliquent a la paire differentielle et au VAS :
- Chaque etage (DiffPair, Cascode, VAS, ClassAB) doit avoir sa propre
  V_bias auto-calculee
- Le feedback du JE-990 utilise le meme schema analytique :
  `Acl = 1 + Rfb/Rg`, applique comme correction de gain
- Le VAS avec compensation Miller (C1=150pF) doit initialiser son filtre
  a la tension de repos du collecteur
- La sortie Class-AB (push-pull) necessite une polarisation symetrique
  pour les deux transistors (Q8 NPN + Q9 PNP)

### Sprint 5 (Output Stage)
- Le crossfade A/B doit gerer le fait que les deux chemins peuvent avoir
  des gains DC residuels differents → filtrer le DC avant crossfade
- T2 recoit le signal post-correction-gain, donc Zout est bien ~11 Ohm
  (pas la Zout "open-loop" des etages)

---

*Document cree suite aux corrections du Sprint 3 (commit S3).
Reference : CEStageWDF.h, EFStageWDF.h, NeveClassAPath.h, test_neve_path.cpp*
