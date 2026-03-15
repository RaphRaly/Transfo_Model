#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include <cmath>

// =============================================================================
// Parameter IDs — used consistently between processor, editor, and state
// =============================================================================
static const juce::String ID_INPUT_LEVEL  = "inputLevel";
static const juce::String ID_OUTPUT_LEVEL = "outputLevel";
static const juce::String ID_MS           = "ms";
static const juce::String ID_A            = "a";
static const juce::String ID_K            = "k";
static const juce::String ID_C            = "c";
static const juce::String ID_ALPHA        = "alpha";
static const juce::String ID_OS_ORDER     = "osOrder";
static const juce::String ID_K_EDDY      = "kEddy";
static const juce::String ID_K_EXCESS    = "kExcess";

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

    // Ms — Saturation magnetization (A/m)
    // Literature Si-Fe: 1.0e6 – 1.7e6
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_MS, 1),
        "Saturation (Ms)",
        juce::NormalisableRange<float>(0.8e6f, 2.0e6f, 1000.0f, 0.5f),
        1.2e6f,
        juce::AudioParameterFloatAttributes().withLabel("A/m")
    ));

    // a — Anhysteretic shape parameter (A/m)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_A, 1),
        "Shape (a)",
        juce::NormalisableRange<float>(10.0f, 200.0f, 0.1f, 0.5f),
        80.0f,
        juce::AudioParameterFloatAttributes().withLabel("A/m")
    ));

    // k — Coercivity / pinning (A/m)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_K, 1),
        "Coercivity (k)",
        juce::NormalisableRange<float>(10.0f, 500.0f, 0.1f, 0.5f),
        200.0f,
        juce::AudioParameterFloatAttributes().withLabel("A/m")
    ));

    // c — Reversibility coefficient
    // Si-Fe typical: 0.01 – 0.2 (NOT 0.5!)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_C, 1),
        "Reversibility (c)",
        juce::NormalisableRange<float>(0.01f, 0.3f, 0.001f),
        0.1f
    ));

    // alpha — Inter-domain coupling
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_ALPHA, 1),
        "Coupling (alpha)",
        juce::NormalisableRange<float>(1e-5f, 1e-2f, 1e-6f, 0.3f),
        5e-4f
    ));

    // Oversampling order: 1=2x, 2=4x, 3=8x
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ID_OS_ORDER, 1),
        "Oversampling",
        juce::StringArray{"2x", "4x", "8x"},
        2   // Default: 8x (index 2)
    ));

    // K_eddy — Classical eddy current loss coefficient (Bertotti)
    // K1 = d^2 / (12 * rho), typical 0–0.05 for audio transformers
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_K_EDDY, 1),
        "Eddy Loss (K1)",
        juce::NormalisableRange<float>(0.0f, 0.05f, 0.0001f, 0.4f),
        0.0f
    ));

    // K_excess — Excess (anomalous) loss coefficient (Bertotti)
    // Empirical parameter, typical 0–0.20
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ID_K_EXCESS, 1),
        "Excess Loss (K2)",
        juce::NormalisableRange<float>(0.0f, 0.20f, 0.001f, 0.5f),
        0.0f
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
    msParam          = apvts.getRawParameterValue(ID_MS);
    aParam           = apvts.getRawParameterValue(ID_A);
    kParam           = apvts.getRawParameterValue(ID_K);
    cParam           = apvts.getRawParameterValue(ID_C);
    alphaParam       = apvts.getRawParameterValue(ID_ALPHA);
    osOrderParam     = apvts.getRawParameterValue(ID_OS_ORDER);
    kEddyParam       = apvts.getRawParameterValue(ID_K_EDDY);
    kExcessParam     = apvts.getRawParameterValue(ID_K_EXCESS);
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

    const double ms    = static_cast<double>(msParam->load());
    const double a_val = static_cast<double>(aParam->load());
    const double k_val = static_cast<double>(kParam->load());
    const double c_val = static_cast<double>(cParam->load());
    const double alpha = static_cast<double>(alphaParam->load());
    const float kEddy   = kEddyParam->load();
    const float kExcess = kExcessParam->load();

    // Update J-A parameters for all channels
    for (int ch = 0; ch < numChannels && ch < maxChannels; ++ch)
    {
        hysteresis[ch].setMs(ms);
        hysteresis[ch].setA(a_val);
        hysteresis[ch].setK(k_val);
        hysteresis[ch].setC(c_val);
        hysteresis[ch].setAlpha(alpha);
        dynamicLosses[ch].setCoefficients(kEddy, kExcess);
    }

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
                double dBdt = dynamicLosses[ch].computeBilinearDBdt(B_approx);
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
