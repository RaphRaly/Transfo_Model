# DAW Compatibility Matrix

## Test Protocol
For each DAW: insert plugin, switch presets 50x, toggle mode during playback, save/load session.

## Results

| DAW | Format | Platform | Insert | Preset Switch x50 | Mode Switch | Session Save/Load | Status |
|-----|--------|----------|--------|--------------------|-------------|-------------------|--------|
| Pro Tools | AAX | Windows | [ ] | [ ] | [ ] | [ ] | Pending |
| Pro Tools | AAX | macOS | [ ] | [ ] | [ ] | [ ] | Pending |
| Ableton Live 12 | VST3 | Windows | [ ] | [ ] | [ ] | [ ] | Pending |
| Ableton Live 12 | VST3 | macOS | [ ] | [ ] | [ ] | [ ] | Pending |
| Logic Pro | AU | macOS | [ ] | [ ] | [ ] | [ ] | Pending |
| Cubase 14 | VST3 | Windows | [ ] | [ ] | [ ] | [ ] | Pending |
| Cubase 14 | VST3 | macOS | [ ] | [ ] | [ ] | [ ] | Pending |
| Reaper | VST3 | Windows | [ ] | [ ] | [ ] | [ ] | Pending |
| Reaper | VST3 | macOS | [ ] | [ ] | [ ] | [ ] | Pending |
| FL Studio | VST3 | Windows | [ ] | [ ] | [ ] | [ ] | Pending |
| Bitwig Studio | VST3 | Windows | [ ] | [ ] | [ ] | [ ] | Pending |
| Studio One | VST3 | Windows | [ ] | [ ] | [ ] | [ ] | Pending |

## Automated Validation
- pluginval strictness level 5: CI/CD (GitHub Actions, all 3 OS)
- Tests: parameter fuzz, preset recall, sample rate changes, buffer size changes

## Known Issues
(none yet)
