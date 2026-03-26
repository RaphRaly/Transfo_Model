#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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

    // Preamp parameter pointers (Sprint 7)
    preampGainParam_    = apvts_.getRawParameterValue(ParamID::PreampGain);
    preampPathParam_    = apvts_.getRawParameterValue(ParamID::PreampPath);
    preampPadParam_     = apvts_.getRawParameterValue(ParamID::PreampPad);
    preampRatioParam_   = apvts_.getRawParameterValue(ParamID::PreampRatio);
    preampPhaseParam_   = apvts_.getRawParameterValue(ParamID::PreampPhase);
    preampEnabledParam_ = apvts_.getRawParameterValue(ParamID::PreampEnabled);

    // Harrison parameter pointers
    harrisonMicGainParam_ = apvts_.getRawParameterValue(ParamID::HarrisonMicGain);
    harrisonPadParam_     = apvts_.getRawParameterValue(ParamID::HarrisonPad);
    harrisonPhaseParam_   = apvts_.getRawParameterValue(ParamID::HarrisonPhase);
    harrisonSourceZParam_ = apvts_.getRawParameterValue(ParamID::HarrisonSourceZ);
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

        // P1.1: Physical models prepared with default 4x OS
        // (runtime OS factor set per-mode in processBlock)
        physicalModel_[ch].setProcessingMode(ProcessingMode::Physical);
        physicalModel_[ch].setOversamplingFactor(4);
        physicalModel_[ch].prepareToPlay(sr, samplesPerBlock);
    }

    // Prepare preamp models (Sprint 7)
    {
        auto preampCfg = PreampConfig::DualTopology();
        // T2 now uses spec 600 Ohm load (DynamicParallelAdaptor numerical
        // stability fix eliminates the previous low-impedance bug).
        for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
        {
            preampModel_[ch].setConfig(preampCfg);
            preampModel_[ch].prepareToPlay(sr, samplesPerBlock);
        }
    }

    // Prepare Harrison Console Mic Pre
    {
        auto jensenCfg = Presets::getByIndex(0);  // Jensen JT-115K-E
        for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
        {
            harrisonTransformer_[ch].setConfig(jensenCfg);
            harrisonTransformer_[ch].setProcessingMode(ProcessingMode::Realtime);
            harrisonTransformer_[ch].prepareToPlay(sr, samplesPerBlock);

            harrisonMicPre_[ch].setTransformer(&harrisonTransformer_[ch]);
            harrisonMicPre_[ch].prepareToPlay(sr, samplesPerBlock);
        }
        harrisonDryBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    }

    // A2.2: Report latency to host
    updateLatencyReport();
}

void PluginProcessor::releaseResources()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        realtimeModel_[ch].reset();
        physicalModel_[ch].reset();
        preampModel_[ch].reset();
        harrisonTransformer_[ch].reset();
        harrisonMicPre_[ch].reset();
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

    // Read common parameters
    const float inputGainDb  = inputGainParam_->load();
    const float outputGainDb = outputGainParam_->load();
    const float mix          = mixParam_->load();
    const int   circuitIndex = static_cast<int>(circuitParam_->load());
    // 0 = O.D.T Balanced Preamp, 1 = Legacy (Transformer), 2 = Harrison Console

    // A2.2: Non-legacy modes have zero latency — update when switching engines
    if (circuitIndex != 1 && lastModeIndex_ != -1)
    {
        lastModeIndex_ = -1;
        setLatencySamples(0);
    }

    if (circuitIndex == 1)
    {
        // ── Legacy mode: old TransformerModel engine ──────────────────────
        const int presetIndex = static_cast<int>(presetParam_->load());
        const int modeIndex   = static_cast<int>(modeParam_->load());

        // A2.2 + P1.1: Update latency and OS factor when mode changes
        if (modeIndex != lastModeIndex_)
        {
            lastModeIndex_ = modeIndex;
            // P1.1: mode 1 = OS4x, mode 2 = OS2x
            const int osFactor = (modeIndex == 2) ? 2 : 4;
            for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
            {
                physicalModel_[ch].setOversamplingFactor(osFactor);
                physicalModel_[ch].prepareToPlay(
                    static_cast<float>(getSampleRate()), getBlockSize());
            }
            updateLatencyReport();
        }

        if (presetIndex != lastPresetIndex_)
        {
            applyPreset(presetIndex);
            lastPresetIndex_ = presetIndex;
        }

        // Legacy calibration offsets
        const float legacyInputDb  = inputGainDb - 10.0f;
        const float legacyOutputDb = outputGainDb + 15.0f;
        const bool isRealtime = (modeIndex == 0);

        for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
        {
            if (isRealtime)
            {
                realtimeModel_[ch].setInputGain(legacyInputDb);
                realtimeModel_[ch].setOutputGain(legacyOutputDb);
                realtimeModel_[ch].setMix(mix);
                realtimeModel_[ch].setUseWdfCircuit(false);
            }
            else
            {
                physicalModel_[ch].setInputGain(legacyInputDb);
                physicalModel_[ch].setOutputGain(legacyOutputDb);
                physicalModel_[ch].setMix(mix);
                physicalModel_[ch].setUseWdfCircuit(false);
            }
        }

        for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            if (isRealtime)
                realtimeModel_[ch].processBlock(data, data, numSamples);
            else
                physicalModel_[ch].processBlock(data, data, numSamples);

            // Safety: clamp NaN/Inf/denormals to zero (A2.1)
            for (int s = 0; s < numSamples; ++s)
            {
                if (!std::isfinite(data[s]) || std::abs(data[s]) < 1e-15f)
                    data[s] = 0.0f;
            }
        }
    }
    else if (circuitIndex == 0)
    {
        // ── Preamp mode: PreampModel is the main engine ──────────────────
        const int preampGainPos = static_cast<int>(preampGainParam_->load());
        const int preampPath    = static_cast<int>(preampPathParam_->load());
        const bool preampPad    = (preampPadParam_->load() > 0.5f);
        const int preampRatio   = static_cast<int>(preampRatioParam_->load());
        const bool preampPhase  = (preampPhaseParam_->load() > 0.5f);

        for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
        {
            preampModel_[ch].setGainPosition(preampGainPos);
            preampModel_[ch].setPath(preampPath);
            preampModel_[ch].setPadEnabled(preampPad);
            preampModel_[ch].setRatio(preampRatio);
            preampModel_[ch].setPhaseInvert(preampPhase);
            preampModel_[ch].setInputGain(inputGainDb);
            preampModel_[ch].setOutputGain(outputGainDb);
            preampModel_[ch].setMix(mix);

            auto* data = buffer.getWritePointer(ch);
            preampModel_[ch].processBlock(data, data, numSamples);

            // Safety: clamp NaN/Inf/denormals to zero (A2.1)
            for (int s = 0; s < numSamples; ++s)
            {
                if (!std::isfinite(data[s]) || std::abs(data[s]) < 1e-15f)
                    data[s] = 0.0f;
            }
        }
    }
    else
    {
        // ── Harrison Console Mic Pre ────────────────────────────────────
        const float micGain  = harrisonMicGainParam_->load();
        const bool  hPad     = (harrisonPadParam_->load() > 0.5f);
        const bool  hPhase   = (harrisonPhaseParam_->load() > 0.5f);
        const float sourceZ  = harrisonSourceZParam_->load();

        const float inputGainLin  = std::pow(10.0f, inputGainDb / 20.0f);
        const float outputGainLin = std::pow(10.0f, outputGainDb / 20.0f);

        for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
        {
            harrisonMicPre_[ch].setMicGain(micGain);
            harrisonMicPre_[ch].setPadEnabled(hPad);
            harrisonMicPre_[ch].setPhaseReverse(hPhase);
            harrisonMicPre_[ch].setSourceImpedance(sourceZ);

            auto* data = buffer.getWritePointer(ch);

            // Save dry signal for mix
            const auto sz = static_cast<size_t>(numSamples);
            if (harrisonDryBuffer_.size() < sz)
                harrisonDryBuffer_.resize(sz);
            std::copy(data, data + numSamples, harrisonDryBuffer_.data());

            // Apply input gain
            for (int s = 0; s < numSamples; ++s)
                data[s] *= inputGainLin;

            // Harrison signal chain: input scaling → transformer → gain stage
            harrisonMicPre_[ch].processBlock(data, numSamples);

            // Apply output gain and dry/wet mix
            for (int s = 0; s < numSamples; ++s)
            {
                data[s] = mix * (data[s] * outputGainLin)
                        + (1.0f - mix) * harrisonDryBuffer_[static_cast<size_t>(s)];

                // Safety: clamp NaN/Inf/denormals to zero (A2.1)
                if (!std::isfinite(data[s]) || std::abs(data[s]) < 1e-15f)
                    data[s] = 0.0f;
            }
        }
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
// Latency Reporting (A2.2)
// =============================================================================
void PluginProcessor::updateLatencyReport()
{
    const int modeIndex = static_cast<int>(modeParam_->load());
    if (modeIndex == 0)
        setLatencySamples(0);       // Realtime (CPWL+ADAA): < 1 sample, report 0
    else if (modeIndex == 2)
        setLatencySamples(13);      // Physical (J-A+OS2x): single halfband = 13 samples
    else
        setLatencySamples(39);      // Physical (J-A+OS4x): halfband cascade = 39 samples
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
