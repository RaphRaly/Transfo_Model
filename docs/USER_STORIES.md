# Product Backlog — User Stories
## Transfo_Model v3

---

## Personas

| Persona | Description | Primary Goal |
|---------|-------------|-------------|
| **Alex** — Studio Engineer | Mixing/mastering engineer using a DAW (Reaper, Pro Tools). Needs real-time monitoring with low CPU. | Apply transformer color during mixdown without latency |
| **Maya** — Sound Designer | Creates sound effects and textures. Uses offline rendering for maximum quality. | Get the most accurate transformer saturation for final bounce |
| **David** — Acoustic Researcher | Studies magnetic materials. Uses the identification pipeline to characterize transformers from measurements. | Identify J-A parameters from B-H measurements and validate models |

---

## Epic 1: Transformer Simulation

### US-1.1 Real-time transformer processing
**As** Alex (Studio Engineer),
**I want** to apply a transformer simulation to my audio tracks in real-time,
**so that** I can hear the analog coloration while mixing without disrupting my workflow.

**Priority:** Must Have | **Status:** Done

**Acceptance Criteria:**
- [ ] Plugin loads in DAW and processes stereo audio
- [ ] Realtime mode uses CPWL+ADAA (no oversampling)
- [ ] Audio output is colored by transformer nonlinearity (non-identical to input)
- [ ] No audio glitches or dropouts at 44.1 kHz / 512 buffer

### US-1.2 High-fidelity offline rendering
**As** Maya (Sound Designer),
**I want** to render my audio through the full physical model for maximum accuracy,
**so that** my final deliverables have the most authentic transformer character.

**Priority:** Must Have | **Status:** Done

**Acceptance Criteria:**
- [ ] Physical mode uses full J-A solver with 4x oversampling
- [ ] Output quality is measurably closer to real transformer behavior than Realtime mode
- [ ] Mode switch does not crash or corrupt audio state

### US-1.3 Mode switching
**As** Alex (Studio Engineer),
**I want** to switch between Realtime and Physical modes during a session,
**so that** I can monitor in Realtime and bounce in Physical without reloading the plugin.

**Priority:** Must Have | **Status:** Done

**Acceptance Criteria:**
- [ ] Mode switch is instant (no reload delay)
- [ ] No audio artifacts during transition
- [ ] Parameter state is preserved across mode switches

### US-1.4 Drive control via Input Gain
**As** Alex (Studio Engineer),
**I want** to control how hard I drive the transformer with an Input Gain knob,
**so that** I can dial in subtle warmth or aggressive saturation.

**Priority:** Must Have | **Status:** Done

**Acceptance Criteria:**
- [ ] Input Gain range: -40 to +20 dB
- [ ] Gain changes are smoothed (no clicks)
- [ ] Higher gain produces more visible hysteresis in B-H scope
- [ ] Parameter is automatable from DAW

### US-1.5 Output level compensation
**As** Alex (Studio Engineer),
**I want** an Output Gain control to compensate for level changes from the transformer,
**so that** I can A/B compare wet vs dry at matched loudness.

**Priority:** Must Have | **Status:** Done

**Acceptance Criteria:**
- [ ] Output Gain range: -40 to +20 dB
- [ ] Internal +15 dB calibration offset applied
- [ ] Smoothed transitions, automatable

### US-1.6 Dry/Wet parallel mixing
**As** Alex (Studio Engineer),
**I want** a Mix knob to blend between the dry and wet signal,
**so that** I can apply subtle transformer coloration via parallel processing.

**Priority:** Should Have | **Status:** Done

**Acceptance Criteria:**
- [ ] Mix 0% = fully dry (bypass)
- [ ] Mix 100% = fully wet
- [ ] Intermediate values produce correct parallel blend
- [ ] No phase cancellation artifacts

---

## Epic 2: Preset Management

### US-2.1 Factory preset selection
**As** Alex (Studio Engineer),
**I want** to choose from classic transformer presets (Jensen, Neve, API),
**so that** I can quickly get the sound character I need.

**Priority:** Must Have | **Status:** Done

**Acceptance Criteria:**
- [ ] 5 factory presets available: Jensen JT-115K-E, Jensen Harrison, Neve 1073 Input, Neve 1073 Output, API AP2503
- [ ] Preset switch is instant
- [ ] Each preset produces audibly different character

### US-2.2 Import custom presets from JSON
**As** David (Acoustic Researcher),
**I want** to import my own transformer configurations from JSON files,
**so that** I can simulate custom or rare transformers that I've characterized.

**Priority:** Should Have | **Status:** Done

**Acceptance Criteria:**
- [ ] PresetLoader reads JSON with core_geometry, electrical, ja_parameters sections
- [ ] Invalid JSON shows clear error message
- [ ] J-A stability condition validated on import
- [ ] Imported preset appears in preset list

### US-2.3 Export presets to JSON
**As** David (Acoustic Researcher),
**I want** to export a transformer configuration to a JSON file,
**so that** I can share my configurations with colleagues or back them up.

**Priority:** Should Have | **Status:** Done

**Acceptance Criteria:**
- [ ] PresetSerializer produces human-readable JSON
- [ ] Exported file can be re-imported without data loss (round-trip)
- [ ] All parameters preserved with sufficient precision

### US-2.4 Scan preset directory
**As** Alex (Studio Engineer),
**I want** the plugin to automatically find all preset files in a folder,
**so that** I don't have to import them one by one.

**Priority:** Could Have | **Status:** Done

**Acceptance Criteria:**
- [ ] PresetManager::loadFromDirectory() scans a folder recursively
- [ ] Valid presets added to preset list
- [ ] Invalid files logged with errors, not silently ignored

### US-2.5 DAW state save/recall
**As** Alex (Studio Engineer),
**I want** my plugin settings to be saved and recalled with my DAW project,
**so that** I don't lose my settings between sessions.

**Priority:** Must Have | **Status:** Done (via APVTS)

**Acceptance Criteria:**
- [ ] All parameters saved in DAW project state
- [ ] Reopening project restores exact settings
- [ ] Preset index and mode selection preserved

---

## Epic 3: Visualization

### US-3.1 Real-time B-H scope
**As** Maya (Sound Designer),
**I want** to see the B-H hysteresis loop in real-time,
**so that** I can visually understand how the transformer is being driven.

**Priority:** Should Have | **Status:** Done

**Acceptance Criteria:**
- [ ] B-H loop displayed with grid and axis labels
- [ ] Updated at ~30 Hz without UI lag
- [ ] Loop shape reflects current drive level and preset
- [ ] No audio thread blocking

### US-3.2 Convergence monitoring
**As** David (Acoustic Researcher),
**I want** to see solver convergence diagnostics,
**so that** I can verify the numerical stability of my custom transformer models.

**Priority:** Could Have | **Status:** Done

**Acceptance Criteria:**
- [ ] Iteration count displayed per sample
- [ ] Convergence status (pass/fail) visible
- [ ] Failure counter tracks divergence events

---

## Epic 4: Stereo Processing

### US-4.1 TMT stereo variation
**As** Alex (Studio Engineer),
**I want** the plugin to simulate component tolerance variations between L/R channels,
**so that** I get natural analog stereo width like real hardware.

**Priority:** Should Have | **Status:** Done

**Acceptance Criteria:**
- [ ] SVU control (0-5%) adjusts tolerance spread
- [ ] 0% = identical L/R (mono-compatible)
- [ ] Higher % = wider stereo image
- [ ] Variation applied to Rdc, capacitances, leakage inductance
- [ ] Deterministic (same seed = same result)
- [ ] Zero extra CPU cost

---

## Epic 5: Parameter Identification

### US-5.1 Identify J-A parameters from measurements
**As** David (Acoustic Researcher),
**I want** to automatically find the Jiles-Atherton parameters that best fit my B-H measurements,
**so that** I can create accurate transformer models from lab data.

**Priority:** Should Have | **Status:** Done (CLI only)

**Acceptance Criteria:**
- [ ] CMA-ES global search finds initial parameter estimate
- [ ] Levenberg-Marquardt refines to local optimum
- [ ] Cost function includes THD, coercivity, loop closure, peak magnetization
- [ ] Log-space reparametrization for numerical stability
- [ ] Stability constraint (k > alpha*Ms) enforced throughout

### US-5.2 Export CPWL leaf for Realtime mode
**As** David (Acoustic Researcher),
**I want** the identification pipeline to automatically generate a CPWL approximation,
**so that** my custom transformer can also run in Realtime mode with low CPU.

**Priority:** Should Have | **Status:** Done

**Acceptance Criteria:**
- [ ] CPWLFitter generates ascending/descending segments from J-A parameters
- [ ] ADAA antiderivative coefficients precomputed
- [ ] THD validation: CPWL vs J-A < 1 dB
- [ ] Max 32 segments per direction

### US-5.3 Active learning measurement suggestions
**As** David (Acoustic Researcher),
**I want** the system to suggest which operating points I should measure next,
**so that** I can minimize the number of lab measurements needed.

**Priority:** Won't Have (this release) | **Status:** Done (experimental)

**Acceptance Criteria:**
- [ ] CMA-ES ensemble provides uncertainty quantification
- [ ] System recommends amplitude/frequency with highest expected information gain
- [ ] Suggestion provided as (amplitude_Vpp, frequency_Hz) pair

---

## Backlog Summary

| Priority | Total | Done | In Progress | To Do |
|----------|-------|------|-------------|-------|
| Must Have | 9 | 9 | 0 | 0 |
| Should Have | 8 | 8 | 0 | 0 |
| Could Have | 2 | 2 | 0 | 0 |
| Won't Have | 1 | 1 | 0 | 0 |
| **Total** | **20** | **20** | **0** | **0** |

---

## Future Backlog (Next Release)

| ID | Story | Priority |
|----|-------|----------|
| US-6.1 | As Alex, I want an undo/redo for parameter changes | Should Have |
| US-6.2 | As Alex, I want an integrated A/B comparison toggle | Should Have |
| US-6.3 | As David, I want an in-plugin identification wizard (load measurement → identify → create preset) | Could Have |
| US-6.4 | As Maya, I want adaptive oversampling (2x when signal is low, 4x when driving hard) | Could Have |
| US-6.5 | As Alex, I want multi-threaded Physical mode (L/R on separate threads) | Could Have |
| US-6.6 | As David, I want temperature-dependent material parameters | Won't Have |
