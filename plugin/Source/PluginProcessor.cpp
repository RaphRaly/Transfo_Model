#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace transfo;

// =============================================================================
// Constructor / Destructor
// =============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "Parameters", transfo::createParameterLayout())
{
    inputGainParam_  = apvts_.getRawParameterValue(ParamID::InputGain);
    outputGainParam_ = apvts_.getRawParameterValue(ParamID::OutputGain);
    mixParam_        = apvts_.getRawParameterValue(ParamID::Mix);
    presetParam_     = apvts_.getRawParameterValue(ParamID::Preset);
    modeParam_       = apvts_.getRawParameterValue(ParamID::Mode);
    svuParam_        = apvts_.getRawParameterValue(ParamID::SVU);
    circuitParam_    = apvts_.getRawParameterValue(ParamID::Circuit);
}

PluginProcessor::~PluginProcessor() = default;

// =============================================================================
// Prepare / Release
// =============================================================================
void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const float sr = static_cast<float>(sampleRate);
    const int numCh = getTotalNumOutputChannels();

    // Generate TMT offsets
    const float tmtAmount = svuParam_->load();
    toleranceModel_.generateRandomOffsets(tmtAmount);

    // Load default preset
    applyPreset(static_cast<int>(presetParam_->load()));

    // Prepare all models
    for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
    {
        realtimeModel_[ch].setProcessingMode(ProcessingMode::Realtime);
        realtimeModel_[ch].prepareToPlay(sr, samplesPerBlock);

        physicalModel_[ch].setProcessingMode(ProcessingMode::Physical);
        physicalModel_[ch].prepareToPlay(sr, samplesPerBlock);
    }
}

void PluginProcessor::releaseResources()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        realtimeModel_[ch].reset();
        physicalModel_[ch].reset();
    }
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainInput  = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput)
        return false;

    return (mainOutput == juce::AudioChannelSet::mono()
         || mainOutput == juce::AudioChannelSet::stereo());
}

// =============================================================================
// Process Block
// =============================================================================
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Read parameters (offsets: input -10 dB, output +15 dB internal calibration)
    const float inputGainDb  = inputGainParam_->load() - 10.0f;
    const float outputGainDb = outputGainParam_->load() + 15.0f;
    const float mix          = mixParam_->load();
    const int   presetIndex  = static_cast<int>(presetParam_->load());
    const int   modeIndex    = static_cast<int>(modeParam_->load());
    const bool  useWdfCircuit = (static_cast<int>(circuitParam_->load()) == 1);

    // Check preset change
    if (presetIndex != lastPresetIndex_)
    {
        applyPreset(presetIndex);
        lastPresetIndex_ = presetIndex;
    }

    // Update gains on models
    const bool isRealtime = (modeIndex == 0);

    for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
    {
        if (isRealtime)
        {
            realtimeModel_[ch].setInputGain(inputGainDb);
            realtimeModel_[ch].setOutputGain(outputGainDb);
            realtimeModel_[ch].setMix(mix);
            realtimeModel_[ch].setUseWdfCircuit(useWdfCircuit);
        }
        else
        {
            physicalModel_[ch].setInputGain(inputGainDb);
            physicalModel_[ch].setOutputGain(outputGainDb);
            physicalModel_[ch].setMix(mix);
            physicalModel_[ch].setUseWdfCircuit(useWdfCircuit);
        }
    }

    // Process each channel
    for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);

        if (isRealtime)
            realtimeModel_[ch].processBlock(data, data, numSamples);
        else
            physicalModel_[ch].processBlock(data, data, numSamples);
    }
}

// =============================================================================
// Presets
// =============================================================================
void PluginProcessor::applyPreset(int presetIndex)
{
    auto baseCfg = Presets::getByIndex(presetIndex);
    const int numCh = getTotalNumOutputChannels();

    for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
    {
        // Apply TMT tolerance for stereo
        auto channel = (ch == 0) ? ToleranceModel::Channel::Left
                                 : ToleranceModel::Channel::Right;
        auto cfg = toleranceModel_.applyToConfig(baseCfg, channel);

        realtimeModel_[ch].setConfig(cfg);
        physicalModel_[ch].setConfig(cfg);
    }
}

// =============================================================================
// Editor
// =============================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

// =============================================================================
// State
// =============================================================================
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

// =============================================================================
// JUCE Plugin Instantiation
// =============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
