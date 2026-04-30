# Sprint 1 — Status

Baseline measurement of the existing Jensen calibration before any DSP change.

## Acceptance criteria — 4/4 atteints

| # | Criterion | Status |
|---|---|---|
| 1 | CSV checked in (≤ 1 MB → en clair) | ✅ 4 fichiers, total 11.7 KB dans `data/measurements/` |
| 2 | PNGs generated and visible in `docs/measurements/` | ✅ 5 fichiers, total ~241 KB |
| 3 | `docs/VALIDATION_REPORT.md` ne contient plus le mot "Pending" | ✅ vérifié par `grep -n Pending` |
| 4 | CTest `test_validation_smoke` exécute le pipeline sur 3 points et vérifie l'existence des CSVs | ✅ test #25 `validation_smoke`, 1.46 s |

## Mesures before/after

There is no "after" this sprint — Sprint 1 est explicitement *avant* tout
changement DSP (interdiction du sprint plan: "Modifier hScale_, bNorm_, ou
tout autre coefficient calibration AVANT que ce sprint soit fini"). Le
"before" est l'état actuel du repo, persisté dans
`data/measurements/*_2026-04-26.csv`.

### Snapshot critique (cf. § 9 Calibration Gap Summary du rapport)

| Mode | Mean \|Δ\| (dB) | Max \|Δ\| (dB) | PASS @ ±3 dB |
|---|---|---|---|
| Realtime (Artistic) | 31.74 | 59.43 (JT-11ELCF 1 kHz/+4 dBu) | 1 / 9 |
| Physical | 14.94 | 32.31 (JT-11ELCF 20 Hz/+4 dBu) | 2 / 9 |
| Combiné | 23.34 | 59.43 | **3 / 18** |

L'audit avait annoncé "~50 dB sur certains points, ~300x trop". Mesuré:
**+59.4 dB sur le pire point Realtime**, soit ×940 trop, donc encore plus
sévère que l'estimation initiale. Sprint 2 aura matière à travailler.

## Blockers rencontrés

Aucun bloquant. Trois observations consignées comme inputs Sprint 2/3, pas
des blockers Sprint 1 :

1. **Plancher 20 Hz Physical à 1.15 % indépendamment du niveau** — observé sur les 3 points 20 Hz JT-115K-E (-20, -2.5, +1.2 dBu) qui produisent tous la même THD à 0.0005 % près. Symptôme cohérent avec un résidu de transient de démarrage NR / FluxIntegrator HP qui domine la fenêtre d'analyse. À investiguer en Sprint 2.
2. **FR Physical -1.19 dB @ 19.85 kHz** — alors que datasheet annonce ±0.25 dB en bande 20 Hz – 20 kHz. Likely cause: l'HP-α prewarp se comporte différemment quand `hScale` est calculé via Ampère's law. Sprint 2 dépendant.
3. **Insertion loss JT-11ELCF non mesurable** sans override de `loadImpedance` (default = 10 kΩ "modern bridging" ≠ datasheet 600 Ω). Différée Sprint 5 (cleanup).

## Décisions techniques notables

1. **Format CSV unique par (preset, type)** avec colonne `mode` au lieu de fichiers séparés — facilite le plot script (groupby) et la diff Sprint 2.
2. **Réutilisation de `Tests/test_common.h`** depuis `tools/` via include relatif — pas de duplication de Goertzel/measureTHD, et la pipeline reste testable sur les mêmes primitives que les CTests.
3. **Smoke test = pipeline réduit, pas validation numérique** — l'acceptance criterion dit "vérifie que les CSV existent (pas le contenu, juste la non-régression)". Le test #25 délègue la vérification à l'exit code de `validate_jensen --smoke` (CMake checks: `add_test` fails on non-zero exit). Pas d'assertions sur les valeurs THD : ce sera Sprint 2 quand les bornes seront serrées.
4. **FR sweep range** capé à `0.45 × fs` (≈ 19.85 kHz) au lieu des 100 kHz prévus dans le sprint plan, parce qu'on travaille à fs = 44.1 kHz natif (Realtime) sans oversampling. Mesurer 20 kHz – 100 kHz exigera Sprint 4 (vrai polyphase OS) pour ne pas être pollué par l'aliasing du downsample. C'est documenté dans § FR notes.
5. **Pas de déduplication entre FR sweep et FR du test existant** — `Tests/test_freq_response_validation.cpp` produit déjà `Tests/data/validation/fr_*.csv`, mais avec un format différent et un ensemble de presets plus large. Pas réécrit ce sprint pour éviter de toucher du code "qui marche".

## Diff Summary

### Files added
- `tools/validate_jensen.cpp` — pipeline C++ THD + FR
- `tools/plot_validation.py` — générateur 5 PNG
- `data/measurements/thd_JT115KE_2026-04-26.csv`
- `data/measurements/thd_JT11ELCF_2026-04-26.csv`
- `data/measurements/fr_JT115KE_2026-04-26.csv`
- `data/measurements/fr_JT11ELCF_2026-04-26.csv`
- `docs/measurements/JT115KE_thd_vs_level_20Hz.png`
- `docs/measurements/JT115KE_thd_vs_level_1kHz.png`
- `docs/measurements/JT11ELCF_thd_vs_freq.png`
- `docs/measurements/JT115KE_FR.png`
- `docs/measurements/JT11ELCF_FR.png`
- `docs/SPRINT_1_REPORT.md` (ce fichier)

### Files modified
- `Tests/CMakeLists.txt` — `add_executable(validate_jensen ...)` + `add_test(validation_smoke ...)`
- `docs/VALIDATION_REPORT.md` — Pending → mesures réelles, colonnes Realtime/Physical séparées, calibration gap summary §9

### Tests added
- CTest `validation_smoke` (#25): 1.46 s. Smoke uniquement — l'assertion numérique
  reste `test_thd_validation` (#9), inchangée.

### Tests touched
- Aucun test C++ existant modifié. Suite complète: **25/25 verts** (24 existants + smoke).

## Commit & branche suggérés

Branche: `sprint/01-validation-baseline` (per sprint plan §GIT)

Commit unique recommandé:
```
feat(validation): Sprint 1 baseline — measure existing Jensen calibration

Add tools/validate_jensen.cpp + plot_validation.py, persist CSV/PNG to
data/measurements/ + docs/measurements/, replace "Pending" rows in
VALIDATION_REPORT.md with measured values and PASS/FAIL columns, add
calibration gap summary (§9). New CTest validation_smoke confirms the
pipeline runs end-to-end. No DSP code touched — ADR-001 + sprint plan
forbid recalibration before the baseline is committed.
```

# Next sprint readiness

**Ready.** Sprint 2 (recalibration hScale_ / bNorm_) peut démarrer sur
`sprint/02-calibration` à partir du commit baseline. Cibles chiffrées :

- **Mean |Δ|**: 23.34 → ≤ 6 dB (≥ 75 % réduction).
- **|Δ| ≤ 3 dB**: 3/18 → ≥ 12/18.
- **Pire point**: Realtime 11ELCF 1 kHz/+4 dBu (59.4 dB) → ≤ 6 dB.

Le test `test_thd_validation` aura ses `CHECK_RANGE` resserrés à mesure
que la calibration progresse — actuellement les bornes (e.g. `[0.5, 15.0]`
pour un point datasheet à 0.065 %) sont volontairement larges et passe-tout,
conformément au commentaire du fichier "ranges below match the cascade
model's actual output".
