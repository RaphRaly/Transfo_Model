# Premium JUCE Plugin GUI — Skill Reference

> **Scope:** Reusable patterns for building dark-console-aesthetic plugin GUIs in JUCE 8+.
> Extracted from the Twisterion / Transfo_Model codebase and cross-referenced with
> FabFilter, Goodhertz, Arturia, and Apple HIG design principles.

---

## 1. Design Principles

### 1.1 Visual Philosophy

| Principle | Rule | Why |
|-----------|------|-----|
| **Dark-first** | `#1A1A1E` base, never pure black (`#000000`) | Pure black kills perceived depth; a warm-charcoal base lets shadows and highlights register |
| **Functional color** | Each accent maps to exactly one semantic role (amber = gain, green = active, red = danger, cyan = I/O, purple = effect amount) | Users learn color → meaning once; mixing meanings forces them to read labels every time |
| **Hierarchy through luminance** | Background → Panel → Section → Recessed = 4 distinct brightness tiers spaced ~6-8 luminance units apart | Eliminates the need for heavy borders; the eye groups by shade |
| **Zero clutter** | No decorative gradients, no textures, no unnecessary borders. If a visual element doesn't help the user read a value or find a control, remove it | FabFilter & Goodhertz principle — every pixel earns its place |
| **Subtle depth** | Knobs get radial gradients + drop shadows; panels get 1px inset borders; meters get recessed backgrounds | Creates physicality without skeuomorphism |

### 1.2 The "Premium" Bar (Industry References)

- **FabFilter:** Extreme clarity. Custom vector knobs, smooth 60fps animations, snappy tooltips, responsive resize. No wasted space.
- **Goodhertz:** Typography-led. One excellent font, generous whitespace, muted palette with a single accent per plugin.
- **Arturia:** Skeuomorphic depth done tastefully. Soft shadows, embossed knobs, subtle textures.
- **Apple HIG (adapted):** Consistent spacing grid (4px/8px), clear visual hierarchy, accessible contrast ratios (WCAG AA ≥ 4.5:1 for text).

---

## 2. Component Patterns

### 2.1 RotarySlider Struct (Reusable Knob Unit)

```cpp
// A self-contained rotary parameter control: slider + label + APVTS attachment.
struct RotarySlider {
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
```

**Setup helper** — call once per knob in the editor constructor:

```cpp
void setupRotary(RotarySlider& rs,
                 const juce::String& paramID,
                 const juce::String& labelText,
                 juce::Colour accentColour,
                 juce::AudioProcessorValueTreeState& apvts)
{
    auto& s = rs.slider;
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14);
    s.setColour(juce::Slider::textBoxTextColourId, Palette::textValue);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    s.setColour(juce::Slider::textBoxBackgroundColourId, Palette::bgRecessed);

    // Store accent colour for the LookAndFeel to read
    s.getProperties().set("arcColour", (juce::int64)(juce::uint32)accentColour.getARGB());

    // Double-click → default value
    if (auto* param = apvts.getParameter(paramID))
        s.setDoubleClickReturnValue(true,
            param->convertFrom0to1(param->getDefaultValue()));

    addAndMakeVisible(s);

    rs.label.setText(labelText, juce::dontSendNotification);
    rs.label.setJustificationType(juce::Justification::centred);
    rs.label.setFont(juce::Font(10.0f).boldened());
    rs.label.setColour(juce::Label::textColourId, Palette::textSecondary);
    addAndMakeVisible(rs.label);

    rs.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, s);
}
```

### 2.2 Illuminated Toggle Button

```cpp
// In LookAndFeel override:
void drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                      bool shouldDrawAsHighlighted, bool shouldDrawAsDown) override
{
    auto bounds = btn.getLocalBounds().toFloat().reduced(1.0f);
    auto onColour = btn.findColour(juce::ToggleButton::tickColourId);
    bool isOn = btn.getToggleState();

    // Background
    g.setColour(isOn ? onColour.darker(0.5f) : Palette::btnOff);
    g.fillRoundedRectangle(bounds, 3.0f);

    // Inner glow when ON
    if (isOn) {
        g.setColour(onColour.brighter(0.3f).withAlpha(0.3f));
        g.fillRoundedRectangle(bounds.reduced(1.0f), 2.0f);
    }

    // Border
    g.setColour(shouldDrawAsHighlighted ? Palette::dividerLight : Palette::btnBorder);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Label text
    g.setColour(isOn ? juce::Colours::white : Palette::textPrimary);
    g.setFont(juce::Font(10.0f).boldened());
    g.drawText(btn.getButtonText(), bounds, juce::Justification::centred);
}
```

### 2.3 Custom ComboBox

```cpp
void drawComboBox(juce::Graphics& g, int w, int h, bool /*isDown*/,
                  int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                  juce::ComboBox& box) override
{
    auto bounds = juce::Rectangle<float>(0, 0, (float)w, (float)h);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Small down-arrow
    juce::Path arrow;
    float arrowX = (float)w - 14.0f, arrowY = (float)h * 0.5f - 2.0f;
    arrow.addTriangle(arrowX, arrowY, arrowX + 8.0f, arrowY, arrowX + 4.0f, arrowY + 5.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}
```

### 2.4 Metering Components

**Segmented LED Meter** — horizontal bar, per-segment color thresholds:
- 30 segments per channel, 1px gap between segments
- Green < -12 dBu, Yellow -12...-3 dBu, Red ≥ -3 dBu
- Dim unlit segments at alpha 0.1 (not invisible — preserves the scale)
- Peak hold: ~1.5s (45 frames at 30fps), then decay

**VU Needle Meter** — analog-style:
- Piecewise dB-to-angle mapping (non-linear: `pow(norm, 0.7)` for -20...0 dBu range)
- Needle with shadow (offset 1,1), bright tip (last 15%), pivot cap
- Attack/release ballistics: EMA coeff 0.12 (~300ms)
- Red arc for overload zone (0...+3 dBu)

**Saturation Meter** — vertical bar:
- Three-tier coloring: green < 30%, yellow < 70%, red ≥ 70%
- Percentage text overlay centered

---

## 3. Look-and-Feel Setup

### 3.1 Architecture

```
SSLLookAndFeel : public juce::LookAndFeel_V4
├── Embedded fonts (Regular + Bold via BinaryData)
├── getTypefaceForFont() override → route bold/regular
├── drawRotarySlider() override → dark radial-gradient knob
├── drawToggleButton() override → illuminated button
├── drawComboBox() override → dark rounded box + arrow
└── drawPopupMenuBackground() override → dark menu
```

**Apply globally in editor constructor:**
```cpp
setLookAndFeel(&lnf_);
juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
```

**Clean up in destructor:**
```cpp
setLookAndFeel(nullptr);
```

### 3.2 Color Palette Template (Namespace)

```cpp
namespace Palette {
// Background tiers (darkest → lightest)
static const juce::Colour bgDark     {0xFF1A1A1E};  // main canvas
static const juce::Colour bgPanel    {0xFF222226};  // header/footer panels
static const juce::Colour bgSection  {0xFF2A2A2E};  // section header strips
static const juce::Colour bgRecessed {0xFF141418};  // inset areas (meters, text boxes)

// Dividers
static const juce::Colour divider      {0xFF3A3A3E};
static const juce::Colour dividerLight {0xFF4A4A4E};

// Text hierarchy
static const juce::Colour textPrimary   {0xFFCCCCCC};  // labels
static const juce::Colour textSecondary {0xFF888888};  // secondary / dim
static const juce::Colour textValue     {0xFFEEEEEE};  // live values
static const juce::Colour textBrand     {0xFFDDDDDD};  // product name

// Semantic accents — assign ONE meaning per color
static const juce::Colour accentAmber  {0xFFE8A030};  // gain / drive
static const juce::Colour accentGreen  {0xFF4CAF50};  // active / enabled
static const juce::Colour accentRed    {0xFFE53935};  // danger / clip / phase
static const juce::Colour accentCyan   {0xFF4FC3F7};  // I/O / signal flow
static const juce::Colour accentPurple {0xFFAB7CDB};  // effect amount / mix

// Knob surface
static const juce::Colour knobBody   {0xFF2A2A30};
static const juce::Colour knobEdge   {0xFF3E3E44};
static const juce::Colour knobHigh   {0xFF4A4A50};
static const juce::Colour knobShadow {0xFF111115};

// Buttons
static const juce::Colour btnOff    {0xFF2E2E34};
static const juce::Colour btnBorder {0xFF3A3A40};
}
```

### 3.3 Embedded Font Pattern

```cpp
// In CMakeLists.txt:
juce_add_binary_data(FontBinaryData SOURCES
    Resources/YourFont-Regular.ttf
    Resources/YourFont-Bold.ttf)
target_link_libraries(YourPlugin PRIVATE FontBinaryData)

// In LookAndFeel constructor:
typefaceRegular_ = juce::Typeface::createSystemTypefaceFor(
    BinaryData::YourFontRegular_ttf, BinaryData::YourFontRegular_ttfSize);
typefaceBold_ = juce::Typeface::createSystemTypefaceFor(
    BinaryData::YourFontBold_ttf, BinaryData::YourFontBold_ttfSize);
```

**Recommended fonts for plugin GUIs:**
- **Space Grotesk** (current) — geometric, techy, excellent at small sizes
- **Inter** — extremely legible, great hinting, open-source
- **DM Sans** — clean geometric alternative
- **SF Pro** (macOS only) — system font, free hinting on Mac

---

## 4. Layout Strategy

### 4.1 Column-Based Proportional Layout

```
┌──────────────────────────────────────────────────┐
│  HEADER (36px) — Brand + subtitle                │
├────┬──────────┬──────────┬───────────────────────┤
│ IN │  ENGINE  │  OUTPUT  │     ANALYSIS          │
│13% │   22%    │   22%    │       43%             │
│    │          │          │  (scope + meters)     │
├────┴──────────┴──────────┴───────────────────────┤
│  FOOTER (24px) — Live readout                    │
└──────────────────────────────────────────────────┘
```

**Implementation in `resized()`:**

```cpp
void PluginEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(36);
    auto footer = area.removeFromBottom(24);

    const float colRatios[] = { 0.13f, 0.22f, 0.22f, 0.43f };
    int totalW = area.getWidth();

    auto col1 = area.removeFromLeft((int)(totalW * colRatios[0]));
    auto col2 = area.removeFromLeft((int)(totalW * colRatios[1]));
    auto col3 = area.removeFromLeft((int)(totalW * colRatios[2]));
    auto col4 = area; // remainder

    // Position components within each column using removeFromTop/Bottom
    layoutInputColumn(col1);
    layoutEngineColumn(col2);
    layoutOutputColumn(col3);
    layoutAnalysisColumn(col4);
}
```

### 4.2 Within-Column Layout

Use `Rectangle::removeFromTop/Left/Right/Bottom()` for sequential stacking:

```cpp
void layoutInputColumn(juce::Rectangle<int> area)
{
    area.reduce(8, 8); // internal padding
    auto knobH = (area.getHeight() - 22) / 2; // 22px gap between knobs
    inputGain_.slider.setBounds(area.removeFromTop(knobH));
    area.removeFromTop(22); // gap
    svuAmount_.slider.setBounds(area.removeFromTop(knobH));
}
```

### 4.3 Resizability

```cpp
// In constructor:
setResizable(true, true);
constrainer_.setMinimumSize(700, 420);
setConstrainer(&constrainer_);
setSize(860, 520); // default
```

**Best practice:** Use ratios, not hardcoded pixel positions. All layout math should derive from `getLocalBounds()`.

### 4.4 Section Headers (Painted, Not Components)

```cpp
void drawSectionHeader(juce::Graphics& g, juce::Rectangle<int> area,
                       const juce::String& title, juce::Colour accent)
{
    // 2px accent bar at top
    g.setColour(accent);
    g.fillRect(area.removeFromTop(2));
    // Background
    g.setColour(Palette::bgSection);
    g.fillRect(area);
    // Title text
    g.setColour(Palette::textPrimary);
    g.setFont(juce::Font(11.0f).boldened());
    g.drawText(title, area, juce::Justification::centred);
}
```

---

## 5. Parameter Binding

### 5.1 APVTS Setup (ParameterLayout.h Pattern)

```cpp
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioProcessorParameter>> params;

    // Float knob (dB range with 0.1 step)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"inputGain", 1}, "Input Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Choice (combo box)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"circuit", 1}, "Circuit",
        juce::StringArray{"Preamp", "Legacy", "Harrison"}, 0));

    // Bool (toggle button)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"pad", 1}, "PAD", false));

    return { params.begin(), params.end() };
}
```

### 5.2 Attachment Lifecycle

```cpp
// In Editor constructor — create attachments AFTER addAndMakeVisible:
inputGain_.attachment = std::make_unique<SliderAttachment>(apvts, "inputGain", inputGain_.slider);

// In Editor destructor — release attachments BEFORE components are destroyed:
// (unique_ptr handles this automatically if attachments are members)
```

**Critical rule:** Attachments must be destroyed before the component they're attached to. If using raw pointers or non-RAII patterns, explicitly reset in destructor.

### 5.3 Dynamic Visibility (Engine Switching)

```cpp
void updateEngineVisibility()
{
    int engine = (int)*apvts_.getRawParameterValue("circuit");

    // Show/hide controls per engine mode
    preampGain_.slider.setVisible(engine == 0);
    preampGain_.label.setVisible(engine == 0);
    harrisonMicGain_.slider.setVisible(engine == 2);
    harrisonMicGain_.label.setVisible(engine == 2);

    // Trigger re-layout if column content changed
    resized();
    repaint();
}
```

Call from `timerCallback()` — not from parameterChanged listener (avoids message-thread issues).

---

## 6. Animation & Refresh

### 6.1 Timer-Driven Updates (30 Hz)

```cpp
class PluginEditor : public juce::AudioProcessorEditor, public juce::Timer
{
    void timerCallback() override
    {
        // 1. Read processor state (lock-free atomics or FIFO)
        float saturation = processorRef_.getPeakSaturation();
        float inputLevel  = processorRef_.getInputLevel_dBu();
        float outputLevel = processorRef_.getOutputLevel_dBu();

        // 2. Push to visual components
        satMeter_.setSaturation(saturation);
        levelMeter_.setLevels(inputLevel, outputLevel);
        bhScope_.repaint(); // scope reads its own ring buffer

        // 3. Update text readouts
        updateMonitorLabel(inputLevel, outputLevel);

        // 4. Check for mode changes
        updateEngineVisibility();
    }
};
```

### 6.2 Meter Smoothing (EMA)

```cpp
// In setLevels():
smoothedLevel_ = smoothedLevel_ * 0.75f + newLevel * 0.25f; // ~100ms @ 30fps
if (std::abs(smoothedLevel_ - lastPaintedLevel_) > 0.3f)
    repaint(); // only repaint on visible change
```

### 6.3 Peak Hold

```cpp
if (newLevel > peakLevel_) {
    peakLevel_ = newLevel;
    peakHoldCounter_ = 45; // ~1.5s @ 30fps
} else if (peakHoldCounter_ > 0) {
    --peakHoldCounter_;
} else {
    peakLevel_ -= 1.0f; // decay 1 dB/frame
}
```

---

## 7. Common Pitfalls

### Anti-Patterns to Avoid

| Pitfall | Why It's Bad | Fix |
|---------|-------------|-----|
| **`repaint()` on every timer tick** | Forces full redraw even when nothing changed | Threshold-gate: only repaint if value delta > visual threshold |
| **Hardcoded pixel positions** | Breaks on resize, different DPI | Use ratios from `getLocalBounds()` + `removeFromTop/Left` |
| **Parameter listener on audio thread** | UB: listener callbacks dispatch to message thread; if you read/write GUI state from audio thread, race conditions | Use atomics/FIFO for audio→GUI data; only read APVTS from timer |
| **Forgetting `setLookAndFeel(nullptr)` in destructor** | Dangling pointer → crash on teardown | Always null out in `~Editor()` |
| **System fonts** | Different rendering per OS, missing on some systems | Embed TTF via BinaryData, override `getTypefaceForFont()` |
| **Too many colors** | Visual noise, no hierarchy | Max 5 semantic accents + 4 background tiers + 3 text tiers |
| **Giant `resized()` method** | 200+ lines becomes unmaintainable | Extract per-column layout helpers |
| **ComboBox items added before attachment** | Attachment resets selection to stored value; if items aren't populated yet, UB | Always `addItemList()` → then create attachment |
| **`paint()` allocating objects** | Allocations in paint = GC pressure = dropped frames | Pre-allocate Paths, Fonts, Colours as members or statics |

### JUCE-Specific Gotchas

1. **Slider `textBoxStyle` must be set before attachment** — otherwise the text box may not reflect the parameter value correctly on first display.
2. **`setDefaultLookAndFeel()` is global** — it affects ALL plugin instances. If hosting multiple instances, use per-component `setLookAndFeel()` instead.
3. **BinaryData naming** — JUCE converts filenames: `Space-Grotesk-Bold.ttf` → `SpaceGroteskBold_ttf`. Know the mangling rules.
4. **Timer resolution** — `startTimerHz(30)` ≈ 33ms. Don't expect frame-accurate timing. For smooth animation, interpolate based on elapsed time, not frame count.
5. **Thread safety** — `timerCallback()` runs on the message thread. Reading `getRawParameterValue()` is safe (returns `std::atomic<float>*`). But don't call `setValue()` on audio thread.

---

## 8. Where Our Current GUI Falls Short (& Specific Upgrades)

### 8.1 No DPI / Retina Awareness

**Current:** Fixed default size 860×520, resizable but no scale factor handling.

**Upgrade:**
```cpp
// In constructor, query the display scale:
float scale = juce::Desktop::getInstance().getDisplays()
    .getDisplayForPoint(getScreenPosition())->scale;
setSize((int)(860 * scale), (int)(520 * scale));

// Apply AffineTransform in paint() for crisp rendering at 2x:
g.addTransform(juce::AffineTransform::scale(scale));
```

Or use JUCE's built-in `setScaleFactor()` on the editor.

### 8.2 No Hover / Interaction Feedback

**Current:** Knobs and buttons have no hover state.

**Upgrade:** Add subtle brightness shift on mouse enter:
```cpp
void drawRotarySlider(...) override {
    bool hovered = slider.isMouseOver();
    auto bodyCol = hovered ? Palette::knobBody.brighter(0.08f) : Palette::knobBody;
    // ... use bodyCol in gradient
}
```

### 8.3 No Animations / Transitions

**Current:** Engine mode switch is instant (show/hide). Values snap.

**Upgrade ideas:**
- **Fade transitions** when switching engine modes (animate alpha over 150ms)
- **Value tooltip** that fades in on drag, fades out 500ms after release
- **Knob pointer glow** — subtle bloom on the pointer line during drag
- Use `juce::ComponentAnimator` or manual alpha interpolation in timerCallback

### 8.4 No Tooltip / Value Popover

**Current:** Values shown in tiny text box below knob.

**Upgrade:** Floating tooltip near cursor during drag:
```cpp
// In Slider::Listener::sliderDragStarted():
tooltip_.setVisible(true);
tooltip_.setBounds(getMouseXYRelative().x - 30, getMouseXYRelative().y - 30, 60, 20);
// In sliderDragEnded(): fade out over 300ms
```

### 8.5 Monolithic Layout Code

**Current:** `resized()` is 200+ lines in one method.

**Upgrade:** Extract into per-section helpers:
```cpp
void resized() override {
    auto area = getLocalBounds();
    layoutHeader(area.removeFromTop(36));
    layoutFooter(area.removeFromBottom(24));
    layoutColumns(area);
}
```

### 8.6 No Keyboard Navigation / Accessibility

**Current:** No `createAccessibilityHandler()` overrides, no keyboard focus management.

**Upgrade:** Add JUCE accessibility support for DAW screen readers; ensure Tab navigation works between controls.

### 8.7 60fps for Smooth Meters

**Current:** 30Hz timer. Meters can appear slightly choppy on high-refresh displays.

**Upgrade:** Bump to `startTimerHz(60)` for meters/scopes, or use `juce::VBlankAttachment` (JUCE 7.0.3+) to sync with display refresh:
```cpp
vblankAttachment_ = std::make_unique<juce::VBlankAttachment>(this, [this] {
    updateMeters();
});
```

---

## 9. Quick-Start Checklist — New Plugin GUI from Scratch

### Phase 1: Foundation
- [ ] Create `PluginEditor.h/cpp` extending `AudioProcessorEditor` + `Timer`
- [ ] Define color palette namespace (copy Palette template from §3.2, customize accents)
- [ ] Embed font via CMake `juce_add_binary_data` + create `PluginLookAndFeel` class
- [ ] Override `getTypefaceForFont()`, `drawRotarySlider()`, `drawToggleButton()`, `drawComboBox()`
- [ ] Set default size + enable resizing with `ComponentBoundsConstrainer`

### Phase 2: Parameters
- [ ] Define all parameters in `ParameterLayout.h` using `AudioParameterFloat/Choice/Bool`
- [ ] Create `RotarySlider` struct (slider + label + attachment)
- [ ] Write `setupRotary()` helper with accent color + double-click default
- [ ] Wire ComboBox/Button attachments (items first, then attachment)

### Phase 3: Layout
- [ ] Sketch column layout on paper — assign percentage widths
- [ ] Implement `resized()` using ratio-based columns + `removeFromTop/Left`
- [ ] Extract per-section layout helpers (keep `resized()` under 40 lines)
- [ ] Paint section headers with accent bars in `paint()`

### Phase 4: Metering & Visualization
- [ ] Add level meter component (horizontal LED or VU needle)
- [ ] Implement EMA smoothing + threshold-gated `repaint()`
- [ ] Add peak hold (45 frames hold, then decay)
- [ ] Start timer at 30Hz (or use VBlankAttachment for 60fps)

### Phase 5: Polish
- [ ] Add hover states to knobs and buttons
- [ ] Add value tooltip on drag
- [ ] Test at 100%, 125%, 150%, 200% display scaling
- [ ] Profile `paint()` — ensure < 2ms per frame
- [ ] Test in multiple DAWs (different hosts = different OpenGL contexts)
- [ ] Verify `setLookAndFeel(nullptr)` in destructor

### Phase 6: Premium Touches
- [ ] Subtle fade transitions for mode switches
- [ ] Animated knob pointer glow during interaction
- [ ] Smooth resize (no layout jumps)
- [ ] Keyboard navigation between controls
- [ ] Accessibility handler for screen readers

---

## 10. File Template — Minimal Starting Point

```
plugin/
├── Source/
│   ├── PluginProcessor.h/cpp    — DSP + APVTS
│   ├── PluginEditor.h/cpp       — GUI (editor + LookAndFeel)
│   ├── ParameterLayout.h        — All parameter definitions
│   ├── Components/
│   │   ├── LevelMeter.h         — Reusable meter
│   │   └── Scope.h              — Reusable scope display
│   └── LookAndFeel/
│       └── PluginLookAndFeel.h  — All custom drawing
├── Resources/
│   ├── Font-Regular.ttf
│   └── Font-Bold.ttf
└── CMakeLists.txt
```

> **Key sizing reference (Twisterion):** 860×520 default, 700×420 minimum.
> 4 columns: 13% / 22% / 22% / 43%. Header 36px, footer 24px.
> Font sizes: brand 16pt, sections 11pt, labels 10pt, meters 9pt.
> Timer: 30Hz. Meter smoothing: EMA 0.25. Peak hold: 45 frames.
