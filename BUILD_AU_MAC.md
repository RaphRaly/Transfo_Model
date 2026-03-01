# Build AU — Transformer Model v3 pour Logic Pro

## Prérequis

Avant de lancer Claude Code, s'assurer que le Mac a :

1. **Xcode** installé depuis l'App Store (gratuit)
2. **Xcode Command Line Tools** : ouvrir Terminal et taper `xcode-select --install`
3. **Homebrew** : `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
4. **CMake** : `brew install cmake`
5. **Claude Code** : `npm install -g @anthropic-ai/claude-code`

## Instructions pour Claude Code

Copie-colle ce prompt dans Claude Code depuis le dossier du projet :

```
cd ~/Desktop/Transfo_Model

Tu es sur macOS. Le projet Transfo_Model est un plugin audio JUCE (C++17).
Le CMakeLists.txt est déjà configuré avec FORMATS VST3 AU Standalone.
SIMDMath.h a déjà le support ARM NEON pour Apple Silicon.

Fais TOUTES ces étapes dans l'ordre, sans t'arrêter :

ÉTAPE 1 — BUILD
- mkdir -p build-mac && cd build-mac
- Détecte l'architecture du Mac (uname -m) :
  - arm64 (Apple Silicon) : cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64"
  - x86_64 (Intel) : cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="x86_64"
- cmake --build . --config Release -j$(sysctl -n hw.ncpu)
- Vérifie que le fichier existe : ls -la TransformerModel_artefacts/Release/AU/

ÉTAPE 2 — INSTALL
- cp -R "TransformerModel_artefacts/Release/AU/Transformer Model v3.component" ~/Library/Audio/Plug-Ins/Components/
- Vérifie : ls ~/Library/Audio/Plug-Ins/Components/ | grep -i transformer

ÉTAPE 3 — SIGNATURE
- xattr -cr ~/Library/Audio/Plug-Ins/Components/"Transformer Model v3.component"
- codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/"Transformer Model v3.component"

ÉTAPE 4 — RESCAN
- killall -9 AudioComponentRegistrar 2>/dev/null || true
- rm -rf ~/Library/Caches/AudioUnitCache/ 2>/dev/null || true
- rm -rf ~/Library/Caches/com.apple.audiounits.cache 2>/dev/null || true

ÉTAPE 5 — VALIDATION
- auval -a | grep -i "Tmfr"
  → doit afficher : aufx Tm01 Tmfr
  Si rien n'apparaît, attends 5 secondes et réessaie.
- auval -v aufx Tm01 Tmfr
  → doit afficher AU VALIDATION SUCCEEDED à la fin.

Si une étape échoue, montre-moi l'erreur complète et on corrige.
À la fin, dis-moi le résultat de auval.
```

## Après l'installation

1. Ouvrir **Logic Pro**
2. Logic rescanne les AU au démarrage (barre de progression possible)
3. Créer un **projet** avec une **piste audio**
4. Cliquer sur un **slot d'insert** vide
5. **Audio Units → TransformerModelProject → Transformer Model v3**
6. Le plugin s'ouvre avec 4 presets : Jensen JT-115K-E, Neve 1073 Input, Neve 1073 Output, API AP2503

## Si Logic bloque le plugin

1. **Logic Pro → Settings → Plug-in Manager**
2. Trouver "Transformer Model v3" dans la liste
3. Cocher **"Use"** pour forcer l'activation
4. Cliquer **"Reset & Rescan Selection"**
5. Fermer les préférences et réinsérer le plugin

## Rebuild après modification du code

À chaque modification du code source, relancer dans Claude Code :

```
cd ~/Desktop/Transfo_Model/build-mac
cmake --build . --config Release -j$(sysctl -n hw.ncpu)
cp -R "TransformerModel_artefacts/Release/AU/Transformer Model v3.component" ~/Library/Audio/Plug-Ins/Components/
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Transformer Model v3.component"
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/"Transformer Model v3.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

Puis rouvrir Logic Pro (ou fermer/rouvrir le plugin dans Logic).

## Troubleshooting

| Problème | Solution |
|----------|----------|
| `cmake` not found | `brew install cmake` |
| `No Xcode or CLT` | `xcode-select --install` |
| Build fail "no member named..." | Vérifier que le bon CMakeLists.txt est utilisé (AU dans FORMATS) |
| auval ne trouve pas le plugin | Vérifier que le .component est dans ~/Library/Audio/Plug-Ins/Components/ |
| auval FAILED | Coller le log d'erreur complet dans Claude Code pour diagnostic |
| Logic "failed validation" | Plug-in Manager → cocher "Use" → Reset & Rescan |
| Plugin absent du menu Logic | Relancer killall AudioComponentRegistrar puis redémarrer Logic |
| Crash au chargement | Rebuilder en Debug, lancer le Standalone pour tester d'abord |
