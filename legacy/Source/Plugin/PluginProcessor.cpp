#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include <cmath>

// =============================================================================
// Parameter IDs — used consistently between processor, editor, and state
// =============================================================================
static const juce::String ID_INPUT_LEVEL  = "inputLevel";
static const juce::String ID_OUTPUT_LEVEL = "outputLevel";
static const juce::String ID_OS_ORDER     = "osOrder";
static const juce::String ID_DYN_LOSS     = "dynLossAmount";

// =============================================================================
// JT-115K-E preset (Permalloy 80 Ni, 0.35 mm tôle) — Perplexity Deep Research.
// α lowered from 2e-4 to 3e-5 to satisfy k > 1.5·α·Ms (40 > 27 ✓) while keeping
// the material's "very soft" character (low coercivity).
// =============================================================================
namespace Preset_JT115KE
{
    constexpr double Ms    = 6.0e5;    // A/m
    constexpr double a     = 15.0;     // A/m
    constexpr double k     = 40.0;     // A/m
    constexpr double c     = 0.25;
    constexpr double alpha = 3.0e-5;
    constexpr double K1    = 0.02;     // s/m      (eddy, m²/12ρ for 0.35 mm Permalloy)
    constexpr double K2    = 0.05;     // A·m⁻¹·(T/s)⁻⁰·⁵
}

// =============================================================================
// Parameter layout
// =============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Input Level (dB) — drives the core harder at higher levels
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_INPUT_LEVEL, 1),
        "Input Level",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    // Output Level (dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_OUTPUT_LEVEL, 1),
        "Output Level",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    // Oversampling order: 1=2x, 2=4x, 3=8x
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ID_OS_ORDER, 1),
        "Oversampling",
        juce::StringArray{"2x", "4x", "8x"},
        2   // Default: 8x (index 2)
    ));

    // Dynamic Losses amount 0–1 — linearly scales K1/K2 between 0 and preset.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_DYN_LOSS, 1),
        "Dynamic Losses",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("")
    ));

    return { params.begin(), params.end() };
}

// =============================================================================
// Constructor / Destructor
// =============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Cache raw parameter pointers for real-time access
    inputLevelParam  = apvts.getRawParameterValue(ID_INPUT_LEVEL);
    outputLevelParam = apvts.getRawParameterValue(ID_OUTPUT_LEVEL);
    osOrderParam     = apvts.getRawParameterValue(ID_OS_ORDER);
    dynLossParam     = apvts.getRawParameterValue(ID_DYN_LOSS);
}

PluginProcessor::~PluginProcessor() = default;

// =============================================================================
// Prepare / Release
// =============================================================================
void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int osOrder = static_cast<int>(osOrderParam->load()) + 1;  // choice index 0→1, 1→2, 2→3
    const int numCh = getTotalNumOutputChannels();

    // Prepare oversampling
    oversampling.prepare(sampleRate, samplesPerBlock, numCh, osOrder);

    const double osRate = oversampling.getOversampledRate();

    // Prepare hysteresis processors at the oversampled rate, inject the JT‑115K‑E
    // preset and wire the DynamicLosses instance into each hysteresis channel.
    for (int ch = 0; ch < numCh && ch < maxChannels; ++ch)
    {
        // J‑A preset (must run before prepare → enforceStability → recalibrate).
        hysteresis[ch].setMs   (Preset_JT115KE::Ms);
        hysteresis[ch].setA    (Preset_JT115KE::a);
        hysteresis[ch].setK    (Preset_JT115KE::k);
        hysteresis[ch].setC    (Preset_JT115KE::c);
        hysteresis[ch].setAlpha(Preset_JT115KE::alpha);

        hysteresis[ch].prepare(osRate);
        dcBlocker[ch].prepare(osRate);
        dcBlocker[ch].reset();

        dynamicLosses[ch].setSampleRate(osRate);
        dynamicLosses[ch].reset();

        // Inject DynamicLosses into the hysteresis solver and apply Bertotti preset.
        hysteresis[ch].setDynamicLosses(&dynamicLosses[ch]);
        hysteresis[ch].setDynamicLossPreset(Preset_JT115KE::K1, Preset_JT115KE::K2);
        const float amount0 = dynLossParam ? dynLossParam->load() : 1.0f;
        hysteresis[ch].setDynamicLossAmount(amount0);
    }

    // Report latency from oversampling filters
    setLatencySamples(static_cast<int>(oversampling.getLatency()));
}

void PluginProcessor::releaseResources()
{
    oversampling.reset();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainInput  = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    // Support mono and stereo
    if (mainInput != mainOutput)
        return false;

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

// =============================================================================
// Process Block — the audio heart
// =============================================================================
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Read parameters (atomic, lock-free)
    const float inputGainDb  = inputLevelParam->load();
    const float outputGainDb = outputLevelParam->load();
    const float inputGain    = std::pow(10.0f, inputGainDb / 20.0f);
    const float outputGain   = std::pow(10.0f, outputGainDb / 20.0f);

    // Dynamic losses mix (linearly scales K1/K2 preset). Applied once per
    // block — the slider is UI-paced, per-sample updates would just waste cycles.
    const float dynAmount = dynLossParam ? dynLossParam->load() : 1.0f;
    for (int ch = 0; ch < numChannels && ch < maxChannels; ++ch)
        hysteresis[ch].setDynamicLossAmount(dynAmount);

    // ── 1. Apply input gain ──
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.applyGain(ch, 0, numSamples, inputGain);

    // ── 2. Upsample ──
    juce::dsp::AudioBlock<float> block(buffer);
    auto osBlock = oversampling.getOversampler().processSamplesUp(block);

    // ── 3. Process at oversampled rate ──
    const int osNumSamples = static_cast<int>(osBlock.getNumSamples());

    for (int ch = 0; ch < numChannels && ch < maxChannels; ++ch)
    {
        auto* data = osBlock.getChannelPointer(ch);

        for (int i = 0; i < osNumSamples; ++i)
        {
            double sample = static_cast<double>(data[i]);

            // Hysteresis + Bertotti dynamic losses (Baghel/Zirka field separation
            // integrated into the NR residual — see HysteresisProcessor::solveImplicit).
            // Output is normalized flux density B / B_target.
            sample = hysteresis[ch].process(sample);

            // DC blocker (remove offset from asymmetric hysteresis)
            sample = dcBlocker[ch].process(sample);

            data[i] = static_cast<float>(sample);
        }
    }

    // ── 4. Downsample ──
    oversampling.getOversampler().processSamplesDown(block);

    // ── 5. Apply output gain ──
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.applyGain(ch, 0, numSamples, outputGain);
}

// =============================================================================
// Editor
// =============================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

// =============================================================================
// State save/load
// =============================================================================
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// =============================================================================
// JUCE plugin instantiation
// =============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
