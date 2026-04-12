#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include <cmath>

// =============================================================================
// Parameter IDs — used consistently between processor, editor, and state
// =============================================================================
static const juce::String ID_INPUT_LEVEL  = "inputLevel";
static const juce::String ID_OUTPUT_LEVEL = "outputLevel";
static const juce::String ID_OS_ORDER     = "osOrder";

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

    // Prepare hysteresis processors at the oversampled rate
    for (int ch = 0; ch < numCh && ch < maxChannels; ++ch)
    {
        hysteresis[ch].prepare(osRate);
        dcBlocker[ch].prepare(osRate);
        dcBlocker[ch].reset();
        dynamicLosses[ch].setSampleRate(osRate);
        dynamicLosses[ch].reset();
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

            // Hysteresis (J-A with NR8 solver)
            sample = hysteresis[ch].process(sample);

            // Bertotti dynamic losses: widen B-H loop at high frequencies
            if (dynamicLosses[ch].isEnabled())
            {
                double B_approx = sample;
                double dBdt = dynamicLosses[ch].computeDBdt(B_approx);
                double Hdyn = dynamicLosses[ch].computeHfromDBdt(dBdt);
                sample -= Hdyn * 0.001;
                dynamicLosses[ch].commitState(B_approx);
            }

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
