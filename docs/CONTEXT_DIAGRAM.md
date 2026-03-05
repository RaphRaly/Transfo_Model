# Context Diagram

## Transfo_Model v3 -- Physical Audio Transformer Simulation Plugin

| Champ             | Valeur                                                 |
|-------------------|--------------------------------------------------------|
| **Projet**        | Transfo_Model v3                                       |
| **Version**       | 1.0                                                    |
| **Date**          | 2026-03-05                                             |
| **Auteur**        | Product Owner -- Equipe SCRUM Transfo_Model             |

---

## 1. Vue d'ensemble

Le diagramme de contexte montre le systeme Transfo_Model v3 au centre, entoure de ses entites externes et des flux de donnees entrants/sortants. Ce diagramme definit la frontiere du systeme (ce qui est dedans) et les interactions avec l'environnement exterieur (ce qui est dehors).

---

## 2. Diagramme de Contexte (ASCII)

```
                          +------------------------------+
                          |                              |
                          |   B-H Measurement Data       |
                          |   (JSON / CSV files)         |
                          |                              |
                          +-------------+----------------+
                                        |
                              [D5] Measured B-H curves
                              [D5] Material metadata
                                        |
                                        v
+-------------------------+   [D1]   +====================================+   [D2]   +-------------------------+
|                         | -------> ||                                  || -------> |                         |
|     DAW Host            |  Audio   ||                                  ||  Audio   |     DAW Host            |
|  (VST3/AU/AAX API)      |  Input   ||      TRANSFO_MODEL v3            ||  Output  |  (VST3/AU/AAX API)      |
|                         |  Buffer  ||      SYSTEM                      ||  Buffer  |                         |
|  - Reaper               | -------> ||                                  || -------> |  - Mix Bus              |
|  - Logic Pro            |          ||  +------------------------------+||          |  - Master Out           |
|  - Pro Tools            | [D1b]    ||  | TransformerModel<CPWLLeaf>   |||          |                         |
|  - Ableton Live         | Params   ||  | TransformerModel<JA Leaf>    |||          +-------------------------+
|  - Studio One           | -------> ||  | HSIMSolver                   |||
|  - Standalone           |          ||  | OversamplingEngine            |||
|                         | [D1c]    ||  | BHScopeComponent             |||
|                         | State    ||  | ConvergenceGuard             |||
|                         | <------> ||  | ToleranceModel (TMT)         |||
|                         |          ||  +------------------------------+||
+-------------------------+          ||                                  ||
                                     ||  +------------------------------+||
                                     ||  | IdentificationPipeline       |||
                                     ||  |   CMA_ES                    |||
                                     ||  |   LevenbergMarquardt        |||
                                     ||  |   CPWLFitter                |||
                                     ||  |   ObjectiveFunction         |||
                                     ||  +------------------------------+||
                                     ||                                  ||
                                     +====================================+
                                        |                  |
                              [D3]      |                  |     [D4]
                     Preset configs     |                  |  Read material
                     (load at init)     |                  |  JSON files
                                        v                  v
                          +-------------+--+  +--+----------------+
                          |                |  |                    |
                          | Transformer    |  | Material JSON      |
                          | Preset JSON    |  | Data               |
                          | Files          |  | (data/materials/)  |
                          |                |  |                    |
                          | (data/         |  | - mu_metal_80ni    |
                          |  transformers/)|  | - nife_50          |
                          |                |  | - permalloy_1j85   |
                          | - jensen_jt115 |  |                    |
                          | - neve_10468   |  +--------------------+
                          | - neve_li1166  |
                          +----------------+
                                                           |
                                                    [D6]   |
                                               State save  |
                                               /restore    |
                                                           v
                                              +---------------------+
                                              |                     |
                                              |  OS File System     |
                                              |                     |
                                              |  - DAW session file |
                                              |  - Plugin state     |
                                              |    (XML binary)     |
                                              |                     |
                                              +---------------------+
```

---

## 3. Description des entites externes

### 3.1 DAW Host (VST3 / AU / AAX API)

| Attribut        | Detail                                                              |
|-----------------|---------------------------------------------------------------------|
| **Type**        | Systeme externe -- Application hote                                 |
| **Exemples**    | Reaper, Logic Pro, Pro Tools, Ableton Live, Studio One, Standalone  |
| **Interface**   | JUCE AudioProcessor API (VST3, AU, AAX, Standalone wrappers)       |
| **Direction**   | Bidirectionnel                                                      |

**Flux entrants vers le systeme :**
- **D1 -- Audio Input Buffer** : Buffer d'echantillons audio (float32, mono ou stereo, 44.1-192 kHz). Transmis via `processBlock(AudioBuffer<float>&, MidiBuffer&)`.
- **D1b -- Parameter Changes** : Modifications de parametres via l'automation DAW ou l'interface graphique. Transmis via `AudioProcessorValueTreeState` (APVTS) avec lecture atomique.
- **D1c -- State Restore** : Restauration de l'etat du plugin lors du rappel d'une session DAW. Transmis via `setStateInformation(data, size)`.

**Flux sortants depuis le systeme :**
- **D2 -- Audio Output Buffer** : Buffer d'echantillons audio traites (meme format que l'entree). Le signal de sortie est un melange dry/wet selon le parametre Mix.
- **D1c -- State Save** : Sauvegarde de l'etat complet du plugin (6 parametres serialises en XML/binaire). Transmis via `getStateInformation(MemoryBlock&)`.

---

### 3.2 Transformer Preset JSON Files

| Attribut        | Detail                                                              |
|-----------------|---------------------------------------------------------------------|
| **Type**        | Donnees statiques -- Fichiers de configuration                      |
| **Localisation**| `data/transformers/` (embarque dans le depot)                       |
| **Format**      | JSON                                                                |
| **Direction**   | Entrant (lecture seule)                                              |

**Flux entrant vers le systeme :**
- **D3 -- Transformer Configurations** : Parametres electriques et mecaniques des transformateurs reels. Chaque fichier JSON contient :
  - Configuration electrique : ratio de spires (N1, N2), resistances DC (Rdc_primary, Rdc_secondary), capacites parasites (C_sec_shield, C_interwinding), inductances (Lp, L_leakage), impedances source/charge
  - Reponse en frequence : bande passante, CMRR
  - Donnees de validation THD : points de mesure (freq, niveau, THD%)

**Fichiers concernes :**
- `jensen_jt115ke.json` -- Jensen JT-115K-E (entree ligne 1:10)
- `neve_10468_input.json` -- Neve 1073 Input Marinair 10468
- `neve_li1166_output.json` -- Neve 1073 Output LI1166

---

### 3.3 Material JSON Data

| Attribut        | Detail                                                              |
|-----------------|---------------------------------------------------------------------|
| **Type**        | Donnees statiques -- Courbes de magnetisation                       |
| **Localisation**| `data/materials/` (embarque dans le depot)                          |
| **Format**      | JSON                                                                |
| **Direction**   | Entrant (lecture seule)                                              |

**Flux entrant vers le systeme :**
- **D4 -- Material B-H Data** : Courbes de magnetisation et proprietes des materiaux magnetiques. Chaque fichier contient :
  - Metadonnees : nom du materiau, source, temperature
  - Proprietes magnetiques : B_sat (T), coercivite (A/m), permeabilite initiale/maximale
  - Bornes J-A pour CMA-ES : Ms, a, k, alpha, c (min/max par famille de materiau)
  - Points de mesure B-H : tableau `BH_points[]` avec `{H_Am, B_T}`

**Fichiers concernes :**
- `mu_metal_80ni.json` -- Mu-metal 80% NiFe (ASTM A753 Alloy 4)
- `nife_50.json` -- NiFe 50% (Radiometal / Alloy 2)
- `permalloy_1j85_sun2023.json` -- Permalloy 1J85 (Sun et al. 2023)

---

### 3.4 B-H Measurement Data

| Attribut        | Detail                                                              |
|-----------------|---------------------------------------------------------------------|
| **Type**        | Donnees externes -- Mesures experimentales                          |
| **Source**      | Equipement de mesure magnetique (VSM, hysteresisgraphe)             |
| **Format**      | JSON (`{data: [{H, B}, ...]}`)                                     |
| **Direction**   | Entrant (lecture seule, cold path)                                  |

**Flux entrant vers le systeme :**
- **D5 -- Measured B-H Curves** : Donnees de mesure d'hysteresis completes (boucle B-H), utilisees par le pipeline d'identification (`IdentificationPipeline`) pour extraire les parametres J-A d'un materiau inconnu. Chaque fichier contient :
  - Materiau, temperature, source de mesure, frequence d'excitation
  - Tableau de points `{H, B}` representant une ou plusieurs boucles d'hysteresis
  - Les donnees sont validees (filtrage NaN/Inf) et les branches ascendante/descendante sont extraites automatiquement

---

### 3.5 OS File System

| Attribut        | Detail                                                              |
|-----------------|---------------------------------------------------------------------|
| **Type**        | Infrastructure -- Systeme de fichiers du systeme d'exploitation     |
| **Direction**   | Bidirectionnel (lecture/ecriture)                                    |

**Flux bidirectionnels :**
- **D6 -- State Save/Restore** : Le systeme de fichiers est utilise indirectement via la DAW pour la persistance de l'etat du plugin dans les fichiers de session. Le plugin lui-meme n'ecrit pas directement sur le disque -- c'est la DAW qui gere la serialisation via `getStateInformation()` / `setStateInformation()`.
- **D4/D5 -- File Read** : Lecture des fichiers JSON de materiaux et mesures par le pipeline d'identification (cold path, via `std::ifstream`).

---

## 4. Description des flux de donnees

| ID   | Nom                        | Source                  | Destination             | Type          | Frequence          |
|------|----------------------------|-------------------------|-------------------------|---------------|--------------------|
| D1   | Audio Input Buffer         | DAW Host                | Transfo_Model System    | float32[]     | ~44100-192000 Hz   |
| D1b  | Parameter Changes          | DAW Host (automation)   | Transfo_Model System    | atomic<float> | Asynchrone         |
| D1c  | State Save/Restore         | DAW Host <-> System     | Bidirectionnel          | XML/binary    | Sur demande        |
| D2   | Audio Output Buffer        | Transfo_Model System    | DAW Host                | float32[]     | ~44100-192000 Hz   |
| D3   | Transformer Preset Config  | Preset JSON Files       | Transfo_Model System    | JSON          | Au chargement      |
| D4   | Material B-H Data          | Material JSON Files     | Transfo_Model System    | JSON          | Au chargement      |
| D5   | Measured B-H Curves        | B-H Measurement Data    | Transfo_Model System    | JSON          | Cold path (offline) |
| D6   | Plugin State Persistence   | Transfo_Model System    | OS File System          | Binary        | Sur demande DAW    |

---

## 5. Description des interfaces

### 5.1 Interface I1 -- JUCE AudioProcessor API

| Attribut         | Detail                                                             |
|------------------|--------------------------------------------------------------------|
| **Protocole**    | JUCE AudioProcessor callbacks                                      |
| **Format audio** | `juce::AudioBuffer<float>` (interleaved ou non-interleaved)        |
| **Parametres**   | `juce::AudioProcessorValueTreeState` (6 parametres atomiques)      |
| **Etat**         | XML serialise via `createXml()` / `fromXml()`                      |
| **Layouts**      | Mono (1 canal) ou Stereo (2 canaux)                                |
| **Sample rates** | 44100, 48000, 88200, 96000, 176400, 192000 Hz                     |
| **Block sizes**  | 1 a 2048 echantillons (kMaxBlockSize)                              |

**Methodes cles :**
```
prepareToPlay(sampleRate, samplesPerBlock)
processBlock(AudioBuffer<float>&, MidiBuffer&)
getStateInformation(MemoryBlock&)
setStateInformation(data, size)
createEditor() -> PluginEditor
```

### 5.2 Interface I2 -- JSON File Import

| Attribut         | Detail                                                             |
|------------------|--------------------------------------------------------------------|
| **Protocole**    | Lecture fichier via `std::ifstream`                                 |
| **Format**       | JSON (parseur minimal integre, pas de dependance externe)          |
| **Validation**   | Filtrage NaN/Inf, verification des champs obligatoires             |
| **Erreur**       | `std::runtime_error` en cas de fichier introuvable ou mal forme    |

### 5.3 Interface I3 -- BH Scope Data (interne audio->GUI)

| Attribut         | Detail                                                             |
|------------------|--------------------------------------------------------------------|
| **Protocole**    | `SPSCQueue<BHSample, 2048>` (lock-free ring buffer)               |
| **Producteur**   | Thread audio (`processBlockRealtime` / `processBlockPhysical`)    |
| **Consommateur** | Thread GUI (`BHScopeComponent::timerCallback()` a 30 Hz)          |
| **Debit**        | 44100/32 = ~1378 samples/s (Realtime) ou 44100*4/128 = ~1378 samples/s (Physical) |

---

## 6. Perimetre du systeme (Scope)

### 6.1 Ce qui est DANS le perimetre

| Composant                          | Description                                                   |
|------------------------------------|---------------------------------------------------------------|
| TransformerModel<CPWLLeaf>         | Simulation temps reel CPWL+ADAA                               |
| TransformerModel<JilesAthertonLeaf>| Simulation physique J-A+OS4x                                  |
| HSIMSolver                         | Solveur WDF multi-nonlineaire                                 |
| HysteresisModel                    | Modele J-A avec solveur NR implicite                          |
| CPWLLeaf                           | Feuille WDF CPWL directionnelle avec ADAA integre             |
| JilesAthertonLeaf                  | Feuille WDF J-A avec port Z adaptatif                         |
| MEJunction                         | Jonction magneto-electrique (couplage Faraday/Ampere)         |
| TopologicalJunction                | Jonction topologique magnetique (9 ports)                     |
| OversamplingEngine                 | Surechantillonnage 4x (halfband FIR)                          |
| ADAAEngine                         | Antiderivative Antialiasing (1er et 2eme ordre)               |
| ConvergenceGuard                   | Protection anti-click sur non-convergence                     |
| ToleranceModel                     | TMT stereo spread par variations de tolerance                 |
| BHScopeComponent                   | Visualisation B-H temps reel                                  |
| PluginProcessor                    | Moteur audio JUCE (processBlock, APVTS)                       |
| PluginEditor                       | Interface graphique JUCE (knobs, combos, scope)               |
| ParameterLayout                    | Definition centralisee des parametres APVTS                   |
| Presets                            | 5 presets d'usine (Jensen, Neve, API)                         |
| IdentificationPipeline             | Orchestration CMA-ES -> L-M -> CPWL export                   |
| CMA_ES                            | Optimisation globale sans gradient                             |
| LevenbergMarquardt                 | Raffinement local par gradient                                |
| CPWLFitter                         | Conversion J-A -> CPWL pour Realtime                          |
| ObjectiveFunction                  | Fonction cout multi-composantes pour identification            |
| MeasurementData                    | Conteneur de donnees de mesure B-H                            |

### 6.2 Ce qui est HORS du perimetre

| Element                            | Raison                                                        |
|------------------------------------|---------------------------------------------------------------|
| DAW (Reaper, Logic, Pro Tools ...) | Entite externe -- fourni par l'utilisateur                    |
| Equipement de mesure magnetique    | Entite externe -- donnees importees en JSON                   |
| Interface reseau / Cloud           | Exclus par exigence NFR-09 (securite)                         |
| Systeme de licensing               | Non implemente dans v3 (recherche/usage personnel)            |
| Installeur / packaging             | Hors scope du code source                                     |
| Interface MIDI                     | Non requis (effet audio pur, pas de synthetiseur)             |
| Sidechain                          | Non implemente dans v3                                        |
| Multi-mono / Surround              | Hors scope (mono/stereo uniquement)                           |
| Edition graphique avancee          | Hors scope (GUI minimale, pas de skin custom)                 |
| Base de donnees de presets          | Hors scope (presets hardcodes dans le code source)            |

---

## 7. Diagramme de flux detaille -- Chaine de traitement audio

```
                        DAW Host
                           |
                    [Audio Input Buffer]
                           |
                           v
                  +------------------+
                  |  PluginProcessor |
                  |  processBlock()  |
                  +--------+---------+
                           |
              +------------+-------------+
              |                          |
         modeIndex == 0             modeIndex == 1
         (Realtime)                 (Physical)
              |                          |
              v                          v
    +-------------------+     +----------------------+
    | processBlock      |     | processBlock         |
    | Realtime          |     | Physical             |
    |                   |     |                      |
    | Per-sample loop:  |     | 1. Upsample x4       |
    |  1. InputGain     |     |    (HalfbandFilter)  |
    |  2. HP filter     |     | 2. Per-OS-sample:    |
    |  3. H = x*hScale  |     |    InputGain         |
    |  4. M = J-A solve |     |    HP filter          |
    |  5. B = mu0*(H+M) |     |    H = x*hScale      |
    |  6. wet = B*bNorm  |     |    M = J-A solve     |
    |  7. LP filter     |     |    B = mu0*(H+M)     |
    |  8. BH scope push |     |    LP filter          |
    |  9. Mix dry/wet   |     |    BH scope push     |
    | 10. OutputGain    |     | 3. Downsample x4     |
    +-------------------+     |    (HalfbandFilter)  |
              |               | 4. Mix dry/wet       |
              |               | 5. OutputGain        |
              |               +----------------------+
              |                          |
              +------------+-------------+
                           |
                    [Audio Output Buffer]
                           |
                           v
                        DAW Host
```

---

## 8. Diagramme de flux -- Pipeline d'identification

```
    +---------------------+
    | B-H Measurement     |
    | JSON File           |
    +----------+----------+
               |
               | loadFromJSON()
               v
    +---------------------+
    | MeasurementData     |
    | - BHPoint[]         |
    | - Metadata          |
    | - getCoercivity()   |
    | - getRemanence()    |
    | - getBsat()         |
    +----------+----------+
               |
               v
    +============================+
    || IdentificationPipeline   ||
    ||                          ||
    || Phase 0: Init            ||
    ||   - Load bounds          ||
    ||   - Set initial guess    ||
    ||                          ||
    || Phase 1: CMA-ES          ||
    ||   - Population sampling  ||
    ||   - Log-reparametrize    ||
    ||   - Covariance adapt     ||
    ||   - 500 generations      ||
    ||                          ||
    || Phase 2: L-M Polish      ||
    ||   - Jacobian FD          ||
    ||   - Trust region         ||
    ||   - Local refinement     ||
    ||                          ||
    || Phase 3: CPWL Export     ||
    ||   - Simulate J-A cycle   ||
    ||   - Fit CPWL segments    ||
    ||   - Passivity enforce    ||
    ||   - ADAA precompute      ||
    +============================+
               |
               v
    +---------------------+      +---------------------+
    | JAParameterSet      |      | CPWLLeaf            |
    | (Ms, a, alpha,      | ---> | (CPWL segments +    |
    |  k, c, K1, K2)      |      |  ADAA coefficients) |
    +---------------------+      +---------------------+
               |                          |
               v                          v
    +---------------------+      +---------------------+
    | Physical Mode       |      | Realtime Mode       |
    | (J-A + OS4x)        |      | (CPWL + ADAA)       |
    +---------------------+      +---------------------+
```

---

*Document genere par l'equipe SCRUM Transfo_Model -- Version 1.0*
