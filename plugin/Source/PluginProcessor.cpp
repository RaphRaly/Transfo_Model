#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../core/include/core/util/Constants.h"
#include <algorithm>
#include <cmath>

using namespace transfo;

namespace {
float getT2LoadOhms(int t2LoadIndex)
{
    if (t2LoadIndex == 0)
        return 600.0f;
    if (t2LoadIndex == 2)
        return 47000.0f;
    return 10000.0f;
}

// Block-peak in dBu using the same 0 dBu = 0.7746 Vrms reference as PreampModel.
float blockPeakDbu(const float* data, int numSamples)
{
    float peak = 0.0f;
    for (int s = 0; s < numSamples; ++s)
        peak = std::max(peak, std::abs(data[s]));
    if (peak < 1.0e-12f)
        return -120.0f;
    constexpr float kVrefPeak = static_cast<float>(transfo::kDBuRefVrms) * 1.4142135f;
    return 20.0f * std::log10(peak / kVrefPeak);
}
} // namespace

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

    // Harrison parameter pointers
    harrisonMicGainParam_ = apvts_.getRawParameterValue(ParamID::HarrisonMicGain);
    harrisonPadParam_     = apvts_.getRawParameterValue(ParamID::HarrisonPad);
    harrisonPhaseParam_   = apvts_.getRawParameterValue(ParamID::HarrisonPhase);
    harrisonSourceZParam_ = apvts_.getRawParameterValue(ParamID::HarrisonSourceZ);
    harrisonDynLossParam_ = apvts_.getRawParameterValue(ParamID::HarrisonDynLoss);

    t2LoadParam_ = apvts_.getRawParameterValue(ParamID::T2Load);
}

PluginProcessor::~PluginProcessor() = default;

// =============================================================================
// Prepare / Release
// =============================================================================
void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const float sr = static_cast<float>(sampleRate);
    const int numCh = getTotalNumOutputChannels();

    const float tmtAmount = svuParam_->load();
    toleranceModel_.generateRandomOffsets(tmtAmount);

    applyPreset(static_cast<int>(presetParam_->load()));
    applyDoubleLegacyConfigs(static_cast<int>(t2LoadParam_->load()));

    for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
    {
        realtimeModel_[ch].setProcessingMode(ProcessingMode::Realtime);
        realtimeModel_[ch].prepareToPlay(sr, samplesPerBlock);

        physicalModel_[ch].setProcessingMode(ProcessingMode::Physical);
        physicalModel_[ch].setOversamplingFactor(4);
        physicalModel_[ch].prepareToPlay(sr, samplesPerBlock);

        doubleLegacyInputRealtime_[ch].setProcessingMode(ProcessingMode::Realtime);
        doubleLegacyInputRealtime_[ch].prepareToPlay(sr, samplesPerBlock);

        doubleLegacyOutputRealtime_[ch].setProcessingMode(ProcessingMode::Realtime);
        doubleLegacyOutputRealtime_[ch].prepareToPlay(sr, samplesPerBlock);

        doubleLegacyInputPhysical_[ch].setProcessingMode(ProcessingMode::Physical);
        doubleLegacyInputPhysical_[ch].setOversamplingFactor(4);
        doubleLegacyInputPhysical_[ch].prepareToPlay(sr, samplesPerBlock);

        doubleLegacyOutputPhysical_[ch].setProcessingMode(ProcessingMode::Physical);
        doubleLegacyOutputPhysical_[ch].setOversamplingFactor(4);
        doubleLegacyOutputPhysical_[ch].prepareToPlay(sr, samplesPerBlock);
    }

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

    doubleLegacyDryBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    doubleLegacyMidBuffer_.resize(static_cast<size_t>(samplesPerBlock), 0.0f);

    updateLatencyReport();
}

void PluginProcessor::releaseResources()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        realtimeModel_[ch].reset();
        physicalModel_[ch].reset();
        doubleLegacyInputRealtime_[ch].reset();
        doubleLegacyOutputRealtime_[ch].reset();
        doubleLegacyInputPhysical_[ch].reset();
        doubleLegacyOutputPhysical_[ch].reset();
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

    const float inputGainDb  = inputGainParam_->load();
    const float outputGainDb = outputGainParam_->load();
    const float mix          = mixParam_->load();
    const int   circuitIndex = static_cast<int>(circuitParam_->load());
    const bool  usesTransformerMode = (circuitIndex == 0 || circuitIndex == 1);

    // Capture pre-processing input peak (host-side level) before any in-place op.
    float inPeakDbu = -120.0f;
    for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
        inPeakDbu = std::max(inPeakDbu, blockPeakDbu(buffer.getReadPointer(ch), numSamples));

    if (!usesTransformerMode && lastModeIndex_ != -1)
    {
        lastModeIndex_ = -1;
        lastModeCircuitIndex_ = -1;
        setLatencySamples(0);
    }

    if (usesTransformerMode)
    {
        const int modeIndex = static_cast<int>(modeParam_->load());
        if (modeIndex != lastModeIndex_ || circuitIndex != lastModeCircuitIndex_)
        {
            lastModeIndex_ = modeIndex;
            lastModeCircuitIndex_ = circuitIndex;

            const int osFactor = (modeIndex == 2) ? 2 : 4;
            const float sampleRate = static_cast<float>(getSampleRate());
            const int prepareBlock = getBlockSize();

            if (circuitIndex == 1)
                prepareLegacyPhysicalModels(sampleRate, prepareBlock, osFactor);
            else
                prepareDoubleLegacyPhysicalModels(sampleRate, prepareBlock, osFactor);

            updateLatencyReport();
        }
    }

    if (circuitIndex == 1)
    {
        const int presetIndex = static_cast<int>(presetParam_->load());
        const int modeIndex   = static_cast<int>(modeParam_->load());
        const bool isRealtime = (modeIndex == 0);

        if (presetIndex != lastPresetIndex_)
        {
            applyPreset(presetIndex);
            lastPresetIndex_ = presetIndex;
        }

        const float legacyInputDb  = inputGainDb - 10.0f;
        const float legacyOutputDb = outputGainDb + 15.0f;

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

            for (int s = 0; s < numSamples; ++s)
            {
                if (!std::isfinite(data[s]) || std::abs(data[s]) < 1e-15f)
                    data[s] = 0.0f;
            }
        }
    }
    else if (circuitIndex == 0)
    {
        const int modeIndex   = static_cast<int>(modeParam_->load());
        const int t2LoadIndex = static_cast<int>(t2LoadParam_->load());
        const bool isRealtime = (modeIndex == 0);

        if (t2LoadIndex != lastT2LoadIndex_)
            applyDoubleLegacyConfigs(t2LoadIndex);

        const float inputGainLin  = std::pow(10.0f, inputGainDb / 20.0f);
        const float outputGainLin = std::pow(10.0f, outputGainDb / 20.0f);
        const auto sz = static_cast<size_t>(numSamples);

        if (doubleLegacyDryBuffer_.size() < sz)
            doubleLegacyDryBuffer_.resize(sz);
        if (doubleLegacyMidBuffer_.size() < sz)
            doubleLegacyMidBuffer_.resize(sz);

        for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            std::copy(data, data + numSamples, doubleLegacyDryBuffer_.data());

            for (int s = 0; s < numSamples; ++s)
                data[s] *= inputGainLin;

            if (isRealtime)
            {
                doubleLegacyInputRealtime_[ch].setUseWdfCircuit(false);
                doubleLegacyInputRealtime_[ch].setInputGain(0.0f);
                doubleLegacyInputRealtime_[ch].setOutputGain(0.0f);
                doubleLegacyInputRealtime_[ch].setMix(1.0f);

                doubleLegacyOutputRealtime_[ch].setUseWdfCircuit(false);
                doubleLegacyOutputRealtime_[ch].setInputGain(0.0f);
                doubleLegacyOutputRealtime_[ch].setOutputGain(0.0f);
                doubleLegacyOutputRealtime_[ch].setMix(1.0f);

                doubleLegacyInputRealtime_[ch].processBlock(
                    data, doubleLegacyMidBuffer_.data(), numSamples);
                doubleLegacyOutputRealtime_[ch].processBlock(
                    doubleLegacyMidBuffer_.data(), data, numSamples);
            }
            else
            {
                doubleLegacyInputPhysical_[ch].setUseWdfCircuit(false);
                doubleLegacyInputPhysical_[ch].setInputGain(0.0f);
                doubleLegacyInputPhysical_[ch].setOutputGain(0.0f);
                doubleLegacyInputPhysical_[ch].setMix(1.0f);

                doubleLegacyOutputPhysical_[ch].setUseWdfCircuit(false);
                doubleLegacyOutputPhysical_[ch].setInputGain(0.0f);
                doubleLegacyOutputPhysical_[ch].setOutputGain(0.0f);
                doubleLegacyOutputPhysical_[ch].setMix(1.0f);

                doubleLegacyInputPhysical_[ch].processBlock(
                    data, doubleLegacyMidBuffer_.data(), numSamples);
                doubleLegacyOutputPhysical_[ch].processBlock(
                    doubleLegacyMidBuffer_.data(), data, numSamples);
            }

            for (int s = 0; s < numSamples; ++s)
            {
                data[s] = mix * (data[s] * outputGainLin)
                        + (1.0f - mix) * doubleLegacyDryBuffer_[static_cast<size_t>(s)];

                if (!std::isfinite(data[s]) || std::abs(data[s]) < 1e-15f)
                    data[s] = 0.0f;
            }
        }
    }
    else
    {
        const float micGain  = harrisonMicGainParam_->load();
        const bool  hPad     = (harrisonPadParam_->load() > 0.5f);
        const bool  hPhase   = (harrisonPhaseParam_->load() > 0.5f);
        const float sourceZ  = harrisonSourceZParam_->load();
        const float dynMix   = std::clamp(harrisonDynLossParam_->load(), 0.0f, 1.0f);

        constexpr float kK1_JT115KE = 1.44e-3f;
        constexpr float kK2_JT115KE = 0.02f;

        const float inputGainLin  = std::pow(10.0f, inputGainDb / 20.0f);
        const float outputGainLin = std::pow(10.0f, outputGainDb / 20.0f);

        for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
        {
            harrisonMicPre_[ch].setMicGain(micGain);
            harrisonMicPre_[ch].setPadEnabled(hPad);
            harrisonMicPre_[ch].setPhaseReverse(hPhase);
            harrisonMicPre_[ch].setSourceImpedance(sourceZ);
            harrisonTransformer_[ch].setDynamicLossCoefficients(
                kK1_JT115KE * dynMix, kK2_JT115KE * dynMix);

            auto* data = buffer.getWritePointer(ch);
            const auto hSz = static_cast<size_t>(numSamples);
            if (harrisonDryBuffer_.size() < hSz)
                harrisonDryBuffer_.resize(hSz);
            std::copy(data, data + numSamples, harrisonDryBuffer_.data());

            for (int s = 0; s < numSamples; ++s)
                data[s] *= inputGainLin;

            harrisonMicPre_[ch].processBlock(data, numSamples);

            for (int s = 0; s < numSamples; ++s)
            {
                data[s] = mix * (data[s] * outputGainLin)
                        + (1.0f - mix) * harrisonDryBuffer_[static_cast<size_t>(s)];

                if (!std::isfinite(data[s]) || std::abs(data[s]) < 1e-15f)
                    data[s] = 0.0f;
            }
        }
    }

    // Publish post-processing peaks for the GUI.
    float outPeakDbu = -120.0f;
    float outRawPeak = 0.0f;
    for (int ch = 0; ch < numChannels && ch < kMaxChannels; ++ch)
    {
        const float* d = buffer.getReadPointer(ch);
        outPeakDbu = std::max(outPeakDbu, blockPeakDbu(d, numSamples));
        for (int s = 0; s < numSamples; ++s)
            outRawPeak = std::max(outRawPeak, std::abs(d[s]));
    }
    levelInDbu_.store(inPeakDbu,  std::memory_order_relaxed);
    levelOutDbu_.store(outPeakDbu, std::memory_order_relaxed);
    isClipping_.store(outRawPeak > 1.0f, std::memory_order_relaxed);
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
        auto channel = (ch == 0) ? ToleranceModel::Channel::Left
                                 : ToleranceModel::Channel::Right;
        auto cfg = toleranceModel_.applyToConfig(baseCfg, channel);

        realtimeModel_[ch].setConfig(cfg);
        physicalModel_[ch].setConfig(cfg);
    }
}

void PluginProcessor::applyDoubleLegacyConfigs(int t2LoadIndex)
{
    auto inputBase = TransformerConfig::Jensen_JT115KE();
    auto outputBase = TransformerConfig::Jensen_JT11ELCF();
    outputBase.loadImpedance = getT2LoadOhms(t2LoadIndex);

    const int numCh = getTotalNumOutputChannels();
    for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
    {
        auto channel = (ch == 0) ? ToleranceModel::Channel::Left
                                 : ToleranceModel::Channel::Right;
        auto inputCfg = toleranceModel_.applyToConfig(inputBase, channel);
        auto outputCfg = toleranceModel_.applyToConfig(outputBase, channel);

        doubleLegacyInputRealtime_[ch].setConfig(inputCfg);
        doubleLegacyOutputRealtime_[ch].setConfig(outputCfg);
        doubleLegacyInputPhysical_[ch].setConfig(inputCfg);
        doubleLegacyOutputPhysical_[ch].setConfig(outputCfg);
    }

    lastT2LoadIndex_ = t2LoadIndex;
}

void PluginProcessor::prepareLegacyPhysicalModels(float sampleRate, int samplesPerBlock, int osFactor)
{
    const int numCh = getTotalNumOutputChannels();
    for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
    {
        physicalModel_[ch].setOversamplingFactor(osFactor);
        physicalModel_[ch].prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void PluginProcessor::prepareDoubleLegacyPhysicalModels(float sampleRate, int samplesPerBlock, int osFactor)
{
    const int numCh = getTotalNumOutputChannels();
    for (int ch = 0; ch < numCh && ch < kMaxChannels; ++ch)
    {
        doubleLegacyInputPhysical_[ch].setOversamplingFactor(osFactor);
        doubleLegacyInputPhysical_[ch].prepareToPlay(sampleRate, samplesPerBlock);

        doubleLegacyOutputPhysical_[ch].setOversamplingFactor(osFactor);
        doubleLegacyOutputPhysical_[ch].prepareToPlay(sampleRate, samplesPerBlock);
    }
}

// =============================================================================
// Latency Reporting (A2.2)
// =============================================================================
void PluginProcessor::updateLatencyReport()
{
    const int circuitIndex = static_cast<int>(circuitParam_->load());
    const int modeIndex    = static_cast<int>(modeParam_->load());

    // Realtime path is sub-sample (CPWL+ADAA, no OS) for every engine.
    if (modeIndex == 0)
    {
        setLatencySamples(0);
        return;
    }

    // Physical path: round-trip halfband cascade. Double Legacy chains two
    // TransformerModel instances, each running its own OS, so the latency adds.
    const int perStage = (modeIndex == 2) ? 13 : 39;  // OS2x or OS4x
    const int stages   = (circuitIndex == 0) ? 2 : 1;
    setLatencySamples(perStage * stages);
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
