#pragma once

// =============================================================================
// PluginProcessor — JUCE AudioProcessor for the Transformer Model plugin.
//
// v3 Architecture: uses TransformerModel<CPWLLeaf> for Realtime mode
// and TransformerModel<JilesAthertonLeaf> for Physical mode.
//
// Processing chain:
//   Realtime:  Input → TransformerModel (CPWL+ADAA, no OS) → Output
//   Physical:  Input → TransformerModel (J-A, OS 4x) → Output
//
// Stereo TMT: slightly different configs per channel for analog spread.
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "../../core/include/core/model/TransformerModel.h"
#include "../../core/include/core/model/Presets.h"
#include "../../core/include/core/model/ToleranceModel.h"
#include "../../core/include/core/magnetics/CPWLLeaf.h"
#include "../../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../../core/include/core/magnetics/AnhystereticFunctions.h"
#include "ParameterLayout.h"

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Transformer Model v3"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts_; }

    // Monitoring data for editor
    transfo::TransformerModel<transfo::CPWLLeaf>::MonitorData getMonitorData() const
    {
        return realtimeModel_[0].getMonitorData();
    }

private:
    juce::AudioProcessorValueTreeState apvts_;

    // DSP: one model per channel (stereo)
    static constexpr int kMaxChannels = 2;
    transfo::TransformerModel<transfo::CPWLLeaf> realtimeModel_[kMaxChannels];
    transfo::TransformerModel<transfo::JilesAthertonLeaf<transfo::LangevinPade>> physicalModel_[kMaxChannels];

    transfo::ToleranceModel toleranceModel_;

    // Cached parameter pointers
    std::atomic<float>* inputGainParam_  = nullptr;
    std::atomic<float>* outputGainParam_ = nullptr;
    std::atomic<float>* mixParam_        = nullptr;
    std::atomic<float>* presetParam_     = nullptr;
    std::atomic<float>* modeParam_       = nullptr;
    std::atomic<float>* tmtParam_        = nullptr;
    std::atomic<float>* msParam_         = nullptr;
    std::atomic<float>* aParam_          = nullptr;
    std::atomic<float>* kParam_          = nullptr;
    std::atomic<float>* cParam_          = nullptr;
    std::atomic<float>* alphaParam_      = nullptr;

    int lastPresetIndex_ = -1;

    void applyPreset(int presetIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
