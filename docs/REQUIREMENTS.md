# Software Requirements Specification (SRS)

## Transfo_Model v3 -- Physical Audio Transformer Simulation Plugin

| Champ             | Valeur                                                 |
|-------------------|--------------------------------------------------------|
| **Projet**        | Transfo_Model v3                                       |
| **Version SRS**   | 1.0                                                    |
| **Date**          | 2026-03-05                                             |
| **Auteur**        | Product Owner -- Equipe SCRUM Transfo_Model             |
| **Statut**        | En vigueur                                             |
| **Classification**| Interne                                                |

---

## 1. Introduction

### 1.1 Objet

Ce document definit les exigences fonctionnelles et non fonctionnelles du plugin audio Transfo_Model v3, un simulateur physique de transformateurs audio analogiques. Le systeme est base sur les Wave Digital Filters (WDF), le modele d'hysteresis de Jiles-Atherton (J-A), et l'Antiderivative Antialiasing (ADAA).

### 1.2 Portee

Le systeme couvre :
- Le traitement audio temps reel et offline via plugin VST3/AU/AAX/Standalone
- La simulation physique de transformateurs (mode Realtime CPWL+ADAA et mode Physical J-A+OS4x)
- La gestion de presets de transformateurs reels (Jensen, Neve, API)
- La visualisation B-H en temps reel
- Le pipeline d'identification parametrique (CMA-ES, Levenberg-Marquardt, export CPWL)

### 1.3 Definitions et acronymes

| Acronyme   | Definition                                                            |
|------------|-----------------------------------------------------------------------|
| **WDF**    | Wave Digital Filter -- cadre de simulation de circuits en domaine onde |
| **J-A**    | Jiles-Atherton -- modele d'hysteresis magnetique                      |
| **CPWL**   | Continuous Piecewise-Linear -- approximation lineaire par morceaux    |
| **ADAA**   | Antiderivative Antialiasing -- suppression d'aliasing sans surechantillonnage |
| **HSIM**   | Hybrid Scattering-Impedance Method -- solveur WDF multi-nonlineaire  |
| **OS4x**   | Oversampling 4x -- surechantillonnage facteur 4                       |
| **CMA-ES** | Covariance Matrix Adaptation Evolution Strategy                       |
| **TMT**    | Tolerance Modeling Technology -- spread stereo par tolerance composant |
| **SVU**    | Stereo Variation Units -- parametre de controle TMT (0-5%)            |
| **THD**    | Total Harmonic Distortion                                             |
| **BH**     | Courbe B(H) -- flux magnetique en fonction du champ                   |
| **NR**     | Newton-Raphson -- methode iterative de resolution                     |
| **ME**     | Magneto-Electric -- jonction de couplage WDF                          |
| **DAW**    | Digital Audio Workstation                                             |
| **APVTS**  | AudioProcessorValueTreeState (JUCE)                                   |

### 1.4 References

| Ref   | Source                                                                   |
|-------|--------------------------------------------------------------------------|
| R-01  | Jiles & Atherton, *J. Magn. Magn. Mater.* 61 (1986)                     |
| R-02  | Parker & Valimaki, *IEEE SPL* (2017) -- ADAA                            |
| R-03  | Chowdhury et al., *arXiv:2210.12554* (2022) -- chowdsp_wdf              |
| R-04  | Werner, Stanford thesis -- WDF convergence, rayon spectral               |
| R-05  | Polimi thesis -- Modelisation multiphysique WD, passivite CPWL           |
| R-06  | Brainworx Patent US 10,725,727 -- TMT stereo tolerance                   |
| R-07  | Hansen 2006 -- CMA-ES tutorial                                          |
| R-08  | Jensen JT-115K-E datasheet                                              |
| R-09  | Neve Drawing EDO 71/13 (22/3/72) -- 1073 schematique                    |
| R-10  | Magnetic Shields Ltd -- courbes B-H mu-metal et NiFe-50                  |

---

## 2. Description generale

### 2.1 Perspective produit

Transfo_Model v3 est un plugin audio professionnel qui simule le comportement non lineaire (saturation, hysteresis) des transformateurs audio analogiques utilises dans les consoles, preamplis et egaliseurs classiques (Neve 1073, API 2500, Harrison). Il s'integre dans les DAW standards via les formats VST3, AU, AAX et Standalone.

### 2.2 Fonctions principales

1. Simulation physique de transformateur audio avec deux modes de traitement
2. Cinq presets de transformateurs reels calibres
3. Controle parametrique (gain entree/sortie, mix, mode, spread stereo)
4. Visualisation temps reel de la courbe d'hysteresis B-H
5. Pipeline d'identification pour creer de nouveaux presets a partir de mesures

### 2.3 Utilisateurs cibles

| Persona                | Role                                               |
|------------------------|-----------------------------------------------------|
| Ingenieur du son       | Mixage temps reel, monitoring, production            |
| Sound Designer         | Rendu offline, sound design, mastering               |
| Chercheur acoustique   | Identification de materiaux, validation de modeles   |

### 2.4 Contraintes

- C++17 strict, pas de dependances externes dans `core/` (header-only)
- JUCE 8.0.4 pour la couche plugin
- Pas d'acces reseau, pas de collecte de donnees personnelles
- Compatibilite multi-plateforme : Windows (MSVC 2022), macOS (Clang 14+), Linux (GCC 12+)

---

## 3. Exigences fonctionnelles (FR)

### FR-01 : Traitement audio temps reel -- Mode Realtime (CPWL+ADAA)

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-01                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT traiter le signal audio en temps reel en utilisant le mode Realtime. Ce mode utilise une approximation CPWL directionnelle (ascendante/descendante) de la courbe d'hysteresis avec ADAA 1er ordre integre dans chaque leaf WDF. Aucun surechantillonnage n'est applique.

**Criteres d'acceptation** :
1. Le traitement se fait sample-par-sample via `TransformerModel<CPWLLeaf>::processBlock()`
2. L'ADAA 1er ordre est calcule analytiquement dans `CPWLLeaf::scatterImpl()` via les antiderivees F(x) par segment
3. Le fallback vers l'evaluation directe `f(x)` est active quand `|dx| < kADAAEpsilon` (1e-5)
4. La direction (ASCENDING/DESCENDING) est mise a jour dynamiquement selon le signe de `da`
5. Les segments CPWL supportent jusqu'a `kMaxSegments = 32` par branche

---

### FR-02 : Traitement audio haute fidelite -- Mode Physical (J-A+OS4x)

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-02                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT proposer un mode Physical pour le bounce/rendu offline. Ce mode utilise le modele J-A complet avec solveur implicite Newton-Raphson et surechantillonnage 4x (polyphase halfband FIR).

**Criteres d'acceptation** :
1. Le solveur NR utilise l'integration trapezoidale avec warm-start extrapolatif `M_pred = 2*M_c - M_prev_c`
2. Le surechantillonnage 4x est gere par `OversamplingEngine` (upsample -> process -> downsample)
3. Le solveur NR converge en maximum `kMaxNRIter = 20` iterations avec tolerance `1e-12`
4. La magnetisation M est bornee a `+/- 1.1 * Ms` (safety clamp)
5. Le double-buffering `M_committed_ / M_tentative_` permet le rollback HSIM

---

### FR-03 : Commutation de mode Processing en temps reel

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-03                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : L'utilisateur DOIT pouvoir basculer entre le mode Realtime et le mode Physical via le parametre Mode de l'APVTS. La commutation est effective au prochain bloc de traitement.

**Criteres d'acceptation** :
1. Le parametre `Mode` est un `AudioParameterChoice` avec les valeurs `{"Realtime (CPWL+ADAA)", "Physical (J-A+OS4x)"}`
2. Le `PluginProcessor::processBlock()` aiguille le traitement vers `realtimeModel_` ou `physicalModel_` selon `modeParam_`
3. Les deux modeles sont instancies et prepares au `prepareToPlay()` pour eviter toute allocation dynamique lors de la commutation
4. La commutation ne produit aucun click/pop audible

---

### FR-04 : Presets de transformateurs -- 5 presets d'usine

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-04                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT fournir 5 presets de transformateurs calibres sur des modeles reels :

| Index | Preset                          | Ratio | Materiau noyau     | Application              |
|-------|---------------------------------|-------|--------------------|--------------------------|
| 0     | Jensen JT-115K-E                | 1:10  | Mu-metal 80% NiFe  | Entree ligne/micro       |
| 1     | Jensen Harrison Preamp          | 1:10  | Mu-metal 80% NiFe  | Preampli Harrison        |
| 2     | Neve 1073 Input (10468)         | 1:2   | NiFe 50%           | Entree micro Neve 1073   |
| 3     | Neve 1073 Output (LI1166)      | 5:3   | NiFe 50% (gappe)   | Sortie ligne Neve 1073   |
| 4     | API AP2503                      | 1:5   | GO SiFe            | Sortie ligne API         |

**Criteres d'acceptation** :
1. Chaque preset est defini par une `TransformerConfig` comprenant `CoreGeometry`, `WindingConfig`, `JAParameterSet`, et `loadImpedance`
2. Le preset Jensen Harrison utilise des impedances source/charge modifiees (Rsource=13.6kOhm, Rload=160Ohm)
3. Le preset Neve 1073 Output utilise un air gap de 0.1mm pour lineariser la courbe B-H
4. La selection est instantanee via `Presets::getByIndex(index)`
5. Le changement de preset reconfigure les deux modeles (realtime + physical) via `applyPreset()`

---

### FR-05 : Parametre InputGain

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-05                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT exposer un parametre InputGain reglable de -40 dB a +20 dB avec un pas de 0.1 dB et une valeur par defaut de 0 dB. Un offset interne de -10 dB est applique pour calibrer le point de fonctionnement nominal.

**Criteres d'acceptation** :
1. Le parametre est de type `AudioParameterFloat` avec ID `"inputGain"`
2. Plage : `NormalisableRange<float>(-40.0f, 20.0f, 0.1f)`, defaut : `0.0f`
3. La valeur est convertie en lineaire via `dBtoLinear(inputGainDb - 10.0f)`
4. Le lissage est assure par `SmoothedValue` avec `rampTime = 20ms`
5. Le gain est applique par echantillon dans le processBlock

---

### FR-06 : Parametre OutputGain

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-06                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT exposer un parametre OutputGain reglable de -40 dB a +20 dB avec un pas de 0.1 dB et une valeur par defaut de 0 dB. Un offset interne de +15 dB est applique pour compenser la normalisation du modele.

**Criteres d'acceptation** :
1. Le parametre est de type `AudioParameterFloat` avec ID `"outputGain"`
2. Plage : `NormalisableRange<float>(-40.0f, 20.0f, 0.1f)`, defaut : `0.0f`
3. La valeur est convertie en lineaire via `dBtoLinear(outputGainDb + 15.0f)`
4. Le lissage est assure par `SmoothedValue` avec `rampTime = 20ms`

---

### FR-07 : Parametre Mix (Dry/Wet)

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-07                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT exposer un parametre Mix de 0% (signal sec) a 100% (signal traite) avec un pas de 1% et une valeur par defaut de 100%.

**Criteres d'acceptation** :
1. Le parametre est de type `AudioParameterFloat` avec ID `"mix"`
2. Plage : `NormalisableRange<float>(0.0f, 1.0f, 0.01f)`, defaut : `1.0f`
3. Le melange est calcule par : `output = dry * (1 - mix) + wet * mix`
4. Le lissage est assure par `SmoothedValue`

---

### FR-08 : Parametre SVU (Stereo Variation Units)

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-08                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT exposer un parametre SVU (0% a 5%) qui controle l'amplitude des variations de tolerance entre les canaux gauche et droit (TMT). Le defaut est 2%.

**Criteres d'acceptation** :
1. Le parametre est de type `AudioParameterFloat` avec ID `"svu"`
2. Plage : `NormalisableRange<float>(0.0f, 5.0f, 0.1f)`, defaut : `2.0f`
3. Les offsets sont generes via `ToleranceModel::generateRandomOffsets(svuPercent)`
4. Les variations s'appliquent a `Rdc_primary`, `Rdc_secondary`, `C_sec_shield`, `C_interwinding`, `L_leakage`
5. A SVU=0%, les canaux gauche et droit sont identiques
6. Les offsets sont generes avec un LCG deterministe (seed=42) pour reproductibilite

---

### FR-09 : Traitement stereo avec TMT

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-09                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT supporter le traitement stereo (2 canaux) avec un modele de transformateur independant par canal. Le modele TMT (Tolerance Modeling Technology) applique des variations de tolerance differentes aux canaux L et R pour creer une largeur stereo naturelle.

**Criteres d'acceptation** :
1. Le `PluginProcessor` instancie `kMaxChannels = 2` modeles (realtime et physical)
2. Les layouts mono et stereo sont supportes (`isBusesLayoutSupported`)
3. Le `ToleranceModel` genere des offsets differents pour `Channel::Left` et `Channel::Right`
4. Les offsets sont appliques a la config du transformateur via `applyToConfig(baseCfg, channel)`
5. Le traitement est independant par canal dans `processBlock()`

---

### FR-10 : Visualisation B-H Scope en temps reel

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-10                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT afficher une courbe B-H en temps reel dans l'interface graphique du plugin. Le scope montre le champ magnetique H (axe horizontal) en fonction de l'induction B (axe vertical), permettant de visualiser la boucle d'hysteresis pendant le traitement.

**Criteres d'acceptation** :
1. Le `BHScopeComponent` est un composant JUCE avec rafraichissement a 30 Hz (`startTimerHz(30)`)
2. Les donnees B-H sont transmises du thread audio au thread GUI via `SPSCQueue<BHSample, 2048>` (lock-free)
3. Le sous-echantillonnage est de 1/32 en mode Realtime et 1/128 en mode Physical
4. L'echelle dynamique s'ajuste automatiquement avec lissage (`maxH_ = maxH_ * 0.9 + currentMaxH * 0.1`)
5. Le trace utilise un path JUCE avec effet "glow" (cyan, epaisseur 3px + 1.5px)
6. Une grille centree avec axes H et B est affichee
7. Le buffer circulaire stocke `kMaxPoints = 512` echantillons

---

### FR-11 : Solveur HSIM (Hybrid Scattering-Impedance Method)

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-11                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT implementer le solveur HSIM pour resoudre le circuit WDF du transformateur contenant des elements non lineaires. Le solveur alterne entre scattering des feuilles non lineaires et propagation via les jonctions.

**Criteres d'acceptation** :
1. Le circuit comporte `NumNL=3` feuilles non lineaires (reluctances), `NumME=3` jonctions ME, `NumMagPorts=9`
2. L'algorithme itere : scattering NL -> forward scan -> root scattering -> backward scan -> convergence check
3. L'adaptation des resistances de port est faite tous les `kDefaultAdaptationInterval = 16` echantillons
4. La mise a jour de la matrice de scattering utilise Sherman-Morrison O(N^2)
5. Le nombre maximum d'iterations est `kMaxNRIter = 20`
6. L'epsilon de convergence est adaptatif via `ConvergenceGuard`

---

### FR-12 : Jonction Magneto-Electrique (MEJunction)

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-12                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT implementer la jonction ME qui convertit entre variables d'onde electriques `(ae, be)` et magnetiques `(am, bm)` via les lois de Faraday et Ampere, discretisees par la regle trapezoidale.

**Criteres d'acceptation** :
1. La jonction est configuree avec le nombre de tours et la frequence d'echantillonnage
2. La conversion utilise `scatterFull(inputVoltage, b_nl)` pour produire les ondes magnetiques
3. La memoire d'onde est engagee via `commitMemory()` a chaque echantillon confirme
4. Le `reset()` reinitialise les etats memoire

---

### FR-13 : Filtres d'impedance de circuit (HP + LP)

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-13                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT modeliser l'interaction d'impedance source/charge via deux filtres de circuit :
- **HP** : roll-off basses frequences du a l'impedance source et l'inductance primaire (`fc_hp = Rsource / (2*pi*Lp)`)
- **LP** : roll-off hautes frequences du a la charge secondaire et l'inductance de fuite (`fc_lp = Rload / (2*pi*L_leakage)`)

**Criteres d'acceptation** :
1. Les coefficients `hpAlpha_` et `lpAlpha_` sont calcules dans `configureCircuit()` en fonction de la config du transformateur
2. Le HP utilise un filtre 1er ordre : `hpOut = alpha * (state + x - prev)`
3. Le LP utilise un filtre 1er ordre : `lpState = (1-alpha)*wet + alpha*lpState`
4. Si les valeurs sont hors plage (bypass), `hpAlpha_ = 1.0` et `lpAlpha_ = 0.0`

---

### FR-14 : Pipeline d'identification parametrique

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-14                                                                   |
| **Priorite** | Should Have                                                             |
| **Statut**   | In Progress                                                             |

**Description** : Le systeme DOIT fournir un pipeline d'identification en 4 phases pour extraire les parametres J-A a partir de mesures B-H :

| Phase | Nom                        | Description                                                 |
|-------|----------------------------|-------------------------------------------------------------|
| 0     | Initialisation             | Chargement JSON, validation bornes, initial guess analytique |
| 1     | Recherche globale (CMA-ES) | Optimisation sans gradient, log-reparametrisation            |
| 2     | Raffinement local (L-M)    | Polish Levenberg-Marquardt sur le bassin d'attraction        |
| 3     | Export CPWL                | Generation CPWLLeaf pour le mode Realtime                    |

**Criteres d'acceptation** :
1. `IdentificationPipeline::run()` execute les phases 0 a 2 et retourne un `PipelineResult`
2. `CMA_ES::optimize()` supporte la log-reparametrisation (log(Ms), log(a), log(k), logit(c))
3. Les bornes sont definies par `MaterialFamily` (MuMetal_80NiFe, NiFe_50, GO_SiFe)
4. La fonction objectif multi-composantes supporte : MajorLoopError, CoercivityError, RemanenceError, THDMatchError, BHClosurePenalty, PsychoacousticTHDWeight
5. `exportToRealtime()` convertit les params J-A en `CPWLLeaf` via `CPWLFitter::generateCPWL()`
6. La condition de stabilite `k > alpha * Ms` est validee dans `JAParameterSet::isPhysicallyValid()`

---

### FR-15 : Import/export de donnees de mesure JSON

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-15                                                                   |
| **Priorite** | Should Have                                                             |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT supporter l'import de donnees de mesure B-H depuis des fichiers JSON. Le format attendu inclut les metadonnees (materiau, temperature, source) et un tableau de points (H, B).

**Criteres d'acceptation** :
1. `MeasurementData::loadFromJSON()` charge un fichier JSON sans dependance externe (parseur minimal integre)
2. Les champs supportes : `material`, `source`, `temperature_C`, `frequency_Hz`, `data[]` avec `{H, B}`
3. Les points NaN/Inf sont filtres automatiquement
4. Les metriques derivees sont calculees : `getCoercivity()`, `getRemanence()`, `getBsat()`, `getHmax()`
5. L'extraction des branches ascendante/descendante est supportee
6. Le calcul RMSE contre une simulation est fourni via `computeRMSE()`

---

### FR-16 : Sauvegarde et restauration de l'etat du plugin

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-16                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT sauvegarder et restaurer l'ensemble des parametres du plugin via le mecanisme JUCE d'etat (getStateInformation / setStateInformation). Ceci permet la sauvegarde dans les sessions DAW.

**Criteres d'acceptation** :
1. `getStateInformation()` serialise l'APVTS en XML puis en binaire via `copyXmlToBinary()`
2. `setStateInformation()` deserialisie depuis le binaire via `getXmlFromBinary()` et restaure l'APVTS
3. Tous les 6 parametres (InputGain, OutputGain, Mix, Preset, Mode, SVU) sont sauvegardes et restaures
4. L'etat est compatible avec le rappel de session DAW (host state save/restore)

---

### FR-17 : Formats de plugin

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-17                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT etre compile et distribue dans les formats de plugin suivants :

| Format     | Plateforme       | API Host                        |
|------------|------------------|---------------------------------|
| VST3       | Windows, macOS   | Steinberg VST3 SDK              |
| AU         | macOS            | Apple Audio Unit v2/v3          |
| AAX        | Windows, macOS   | Avid AAX SDK (si `AAX_SDK_PATH` configure) |
| Standalone | Windows, macOS   | JUCE Standalone wrapper         |

**Criteres d'acceptation** :
1. La directive CMake `FORMATS VST3 AU AAX Standalone` est configuree dans `juce_add_plugin()`
2. Le Manufacturer Code est `Tmfr`, le Plugin Code est `Tm01`
3. Le type AU est `kAudioUnitType_Effect`
4. La categorie AAX est `AAX_ePlugInCategory_Harmonic`
5. Aucun MIDI n'est requis (`NEEDS_MIDI_INPUT FALSE`)

---

### FR-18 : Monitoring de convergence

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-18                                                                   |
| **Priorite** | Should Have                                                             |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT afficher en temps reel les metriques de convergence du solveur HSIM dans le bandeau inferieur de l'interface. En mode Debug, le rayon spectral est egalement affiche.

**Criteres d'acceptation** :
1. L'affichage est mis a jour a 10 Hz via `timerCallback()` dans `PluginEditor`
2. Les donnees affichees incluent : nombre d'iterations, statut (OK/FAIL), compteur de failures
3. En debug (`#ifndef NDEBUG`), le rayon spectral `rho` est affiche avec 3 decimales
4. Les donnees transitent du thread audio via `MonitorData` (lecture atomique via `std::atomic`)

---

### FR-19 : Donnees de materiaux et presets JSON

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-19                                                                   |
| **Priorite** | Should Have                                                             |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT inclure des fichiers JSON de reference pour les materiaux magnetiques et les configurations de transformateurs dans le dossier `data/`.

**Criteres d'acceptation** :
1. Le dossier `data/materials/` contient les courbes B-H JSON pour : mu-metal 80% NiFe, NiFe 50%, permalloy 1J85
2. Le dossier `data/transformers/` contient les configs JSON pour : Jensen JT-115K-E, Neve 10468 input, Neve LI1166 output
3. Chaque fichier materiau contient : `B_sat_T`, `coercivity_Am`, `initial_permeability`, `BH_points[]`, `ja_bounds{}`
4. Chaque fichier transformateur contient : `electrical{}`, `frequency_response{}`, `thd_validation[]`

---

### FR-20 : Normalisation de sortie et calibration

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | FR-20                                                                   |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT normaliser la sortie pour un gain unitaire en region lineaire. Le scaling H et la normalisation B sont calcules analytiquement a partir des parametres J-A.

**Criteres d'acceptation** :
1. `hScale_ = material.a * 5.0` mappe le signal +/-1 vers le champ H autour du genou de saturation
2. La susceptibilite lineaire est calculee : `chi0 = Ms*c/(3*a)`, `chiEff = chi0/(1 - alpha*chi0)`
3. Le gain lineaire est normalise : `bNorm_ = 1.0 / (mu0 * (1 + chiEff) * hScale_)`
4. Le denominateur de feedback est borne : `denomFeedback >= 0.1` pour eviter l'instabilite

---

## 4. Exigences non fonctionnelles (NFR)

### NFR-01 : Performance CPU -- Mode Realtime

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-01                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le mode Realtime DOIT consommer moins de 15% du CPU d'un coeur a 44.1 kHz en traitement mono, et moins de 18% en stereo. Le traitement CPWL+ADAA est optimise pour ne necessiter aucun surechantillonnage.

**Criteres de verification** :
1. Mesure via profiling JUCE `AudioProcessorEditor::cpuUsage` ou benchmark externe
2. Configuration de test : buffer 512 samples, 44.1 kHz, processeur Intel i7 ou equivalent
3. Benchmark CPWL : ~80 ns pour 3 feuilles CPWL avec ADAA (0.4% du budget 44.1 kHz)
4. Pas d'allocation dynamique dans le chemin audio (hot path)

---

### NFR-02 : Performance CPU -- Mode Physical

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-02                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le mode Physical DOIT etre fonctionnel pour un rendu offline. La consommation CPU ne doit pas depasser 60% d'un coeur en stereo a 44.1 kHz. Le mode est concu pour le bounce/render, pas pour le monitoring temps reel.

**Criteres de verification** :
1. Mesure avec le meme protocole que NFR-01
2. Le surechantillonnage 4x multiplie la charge par un facteur 4-5x par rapport au mode Realtime

---

### NFR-03 : Latence

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-03                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : La latence du traitement DOIT etre inferieure a 1 ms en mode Realtime. Le mode Physical a une latence supplementaire due au filtre halfband (group delay = 3 echantillons oversample = 0.75 echantillon a la frequence originale).

**Criteres de verification** :
1. `getTailLengthSeconds()` retourne `0.0` (le plugin ne declare pas de latence additionnelle)
2. Le mode Realtime est sample-par-sample, latence = 0 echantillon
3. Le mode Physical a une latence de `kFilterLatency / factor = 3/4 = 0.75 echantillon` (reportee ou compensee)

---

### NFR-04 : Qualite audio -- Fidelite CPWL vs J-A

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-04                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | In Progress                                                             |

**Description** : La difference de THD entre le mode Realtime (CPWL) et le mode Physical (J-A) DOIT etre inferieure a 1 dB pour un signal sinusoidal a 1 kHz, -10 dBu, sur tous les presets.

**Criteres de verification** :
1. Mesure A/B via l'outil `tools/ab_compare.py`
2. Signal de test : sinus 1 kHz, duree 1s, 44.1 kHz
3. Analyse spectrale FFT sur les 10 premiers harmoniques
4. Tolerance : `|THD_CPWL_dB - THD_JA_dB| < 1.0 dB`

---

### NFR-05 : Robustesse -- ConvergenceGuard

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-05                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme NE DOIT PAS produire de crash, NaN, Inf ou click/pop audible en cas de non-convergence du solveur HSIM. Le `ConvergenceGuard` assure une degradation gracieuse.

**Criteres de verification** :
1. Cas de non-convergence : `getSafeOutput()` retourne un blend lisse entre la derniere sortie convergee et le candidat (`alpha = 0.5`)
2. Apres 3 echecs consecutifs, `adaptiveEpsilon` est double (relaxation progressive)
3. L'epsilon adaptatif est borne a `baseEpsilon * 64` pour eviter le bypass complet
4. Le compteur de failures est atomique (`std::atomic<int>`) pour lecture thread-safe depuis l'UI
5. Test : signal d'entree avec transitoire extreme (+20 dB, 20 Hz) ne produit pas de crash

---

### NFR-06 : Stabilite du modele J-A

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-06                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Les parametres J-A DOIVENT respecter la condition de stabilite `k > alpha * Ms` a tout moment. La magnetisation est bornee et la validation physique est appliquee avant toute utilisation.

**Criteres de verification** :
1. `JAParameterSet::isPhysicallyValid()` verifie : Ms > 0, a > 0, k > 0, alpha >= 0, c in [0,1], k > alpha*Ms
2. `clampToValid()` force la condition : si `k <= alpha*Ms`, alors `k = alpha*Ms*1.1`
3. Le solveur Newton-Raphson borne la magnetisation a `+/- 1.1 * Ms`
4. Test : 65 tests passent dans `test_cpwl_passivity` (passivite, stabilite J-A, LangevinPade)

---

### NFR-07 : Portabilite multi-plateforme

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-07                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | In Progress                                                             |

**Description** : Le systeme DOIT compiler et fonctionner sur les plateformes suivantes :

| Plateforme | Compilateur      | Statut      |
|------------|------------------|-------------|
| Windows    | MSVC 2022        | Done        |
| macOS      | Clang 14+        | To Do       |
| Linux      | GCC 12+          | To Do       |

**Criteres de verification** :
1. CMake 3.22+ est le systeme de build
2. Le standard C++17 est impose (`CMAKE_CXX_STANDARD 17`)
3. La librairie `core/` est header-only avec zero dependance externe
4. Le CI GitHub Actions valide la compilation sur les 3 plateformes

---

### NFR-08 : Thread Safety

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-08                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT respecter les contraintes de thread safety entre le thread audio (temps reel) et le thread GUI. Aucun lock, allocation ou operation bloquante n'est autorise sur le thread audio.

**Criteres de verification** :
1. Les parametres sont lus via `std::atomic<float>*` (APVTS raw parameter)
2. Les donnees B-H transitent via `SPSCQueue` (lock-free, single producer single consumer)
3. Le compteur de failures utilise `std::atomic<int>` avec `memory_order_relaxed`
4. Aucune allocation dynamique dans `processBlock()` (pre-allocation dans `prepareToPlay()`)
5. Le `ScopedNoDenormals` est actif dans `processBlock()`

---

### NFR-09 : Securite et vie privee

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-09                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le systeme NE DOIT PAS acceder au reseau, collecter des donnees personnelles, ni transmettre d'informations a l'exterieur. Le plugin fonctionne integralement hors ligne.

**Criteres de verification** :
1. `JUCE_WEB_BROWSER=0` desactive le navigateur web integre
2. `JUCE_USE_CURL=0` desactive libcurl
3. Aucun appel reseau dans le code source (verification par grep)
4. Aucune ecriture sur le systeme de fichiers sauf le state save/restore standard JUCE
5. Aucune collecte de telemetrie ou analytics

---

### NFR-10 : Testabilite

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-10                                                                  |
| **Priorite** | Should Have                                                             |
| **Statut**   | Done                                                                    |

**Description** : Le systeme DOIT disposer de suites de tests automatises couvrant les composants critiques.

**Criteres de verification** :
1. `test_cpwl_adaa` : 22 tests (continuite antiderivee, ADAA 1er/2eme ordre, commutation direction, suppression aliasing)
2. `test_cpwl_passivity` : 65 tests (passivite, validite J-A, log-space round-trip, LangevinPade, HysteresisModel commit/rollback, DynamicLosses)
3. `test_hsim_diagnostics` : diagnostics solveur HSIM
4. `test_hysteresis` : tests heritage Phase 1 J-A
5. Les tests sont compilables independamment via `Tests/CMakeLists.txt`

---

### NFR-11 : Maintenabilite architecturale

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-11                                                                  |
| **Priorite** | Should Have                                                             |
| **Statut**   | Done                                                                    |

**Description** : L'architecture DOIT respecter une separation stricte en couches avec un ordre de dependance unidirectionnel :

```
1. core/           Header-only, ZERO dependances externes
2. identification/  Depend de core/, peut utiliser Eigen (cold path)
3. plugin/          JUCE + core/ + identification/
4. tools/           CLI tools (optionnel)
```

**Criteres de verification** :
1. `core/` ne contient aucun `#include` vers JUCE, Eigen, ou toute autre librairie externe
2. La librairie `TransformerCore` est declaree `INTERFACE` dans CMake
3. Le namespace `transfo` est utilise dans tout le code core et identification
4. Les templates (CRTP pour WDOnePort, template sur AnhystType pour HysteresisModel) permettent le polymorphisme sans vtable

---

### NFR-12 : Zero allocation sur le hot path

| Champ        | Detail                                                                  |
|--------------|-------------------------------------------------------------------------|
| **ID**       | NFR-12                                                                  |
| **Priorite** | Must Have                                                               |
| **Statut**   | Done                                                                    |

**Description** : Le chemin audio (hot path) NE DOIT effectuer aucune allocation dynamique (heap). Tous les buffers sont pre-alloues dans `prepareToPlay()`.

**Criteres de verification** :
1. `AlignedBuffer` et `AlignedArray` utilisent des tableaux de taille fixe ou pre-alloues
2. `SPSCQueue` utilise un buffer de taille fixe (template `N=2048`)
3. `std::array` est utilise pour les segments CPWL (`kMaxSegments = 32`)
4. Les modeles `TransformerModel` sont instancies comme membres (pas de pointeur dynamique)
5. Le buffer d'oversampling est alloue dans `OversamplingEngine::prepare()`

---

## 5. Matrice de tracabilite

| Exigence | Epic associee              | Tests de verification              | Statut       |
|----------|----------------------------|------------------------------------|--------------|
| FR-01    | Transformer Simulation     | test_cpwl_adaa                     | Done         |
| FR-02    | Transformer Simulation     | test_hysteresis, test_cpwl_passivity | Done       |
| FR-03    | Transformer Simulation     | Integration manuelle               | Done         |
| FR-04    | Preset Management          | Integration manuelle               | Done         |
| FR-05    | Transformer Simulation     | Integration manuelle               | Done         |
| FR-06    | Transformer Simulation     | Integration manuelle               | Done         |
| FR-07    | Transformer Simulation     | Integration manuelle               | Done         |
| FR-08    | Stereo Processing          | Integration manuelle               | Done         |
| FR-09    | Stereo Processing          | Integration manuelle               | Done         |
| FR-10    | Visualization              | Integration manuelle               | Done         |
| FR-11    | Transformer Simulation     | test_hsim_diagnostics              | Done         |
| FR-12    | Transformer Simulation     | test_hsim_diagnostics              | Done         |
| FR-13    | Transformer Simulation     | Integration manuelle               | Done         |
| FR-14    | Parameter Identification   | Placeholder tests                  | In Progress  |
| FR-15    | Parameter Identification   | test_cpwl_passivity                | Done         |
| FR-16    | Preset Management          | Integration DAW                    | Done         |
| FR-17    | Transformer Simulation     | Build CI                           | Done         |
| FR-18    | Visualization              | Integration manuelle               | Done         |
| FR-19    | Preset Management          | Integration manuelle               | Done         |
| FR-20    | Transformer Simulation     | test_cpwl_passivity                | Done         |
| NFR-01   | Performance                | Benchmark CPU                      | Done         |
| NFR-02   | Performance                | Benchmark CPU                      | Done         |
| NFR-03   | Performance                | Mesure latence                     | Done         |
| NFR-04   | Quality                    | ab_compare.py                      | In Progress  |
| NFR-05   | Robustesse                 | test_hsim_diagnostics              | Done         |
| NFR-06   | Robustesse                 | test_cpwl_passivity                | Done         |
| NFR-07   | Portabilite                | CI multi-plateforme                | In Progress  |
| NFR-08   | Thread Safety              | Code review                        | Done         |
| NFR-09   | Securite                   | Code review, grep                  | Done         |
| NFR-10   | Testabilite                | test_cpwl_adaa, test_cpwl_passivity | Done        |
| NFR-11   | Maintenabilite             | Architecture review                | Done         |
| NFR-12   | Performance                | Code review, profiling             | Done         |

---

*Document genere par l'equipe SCRUM Transfo_Model -- Version 1.0*
