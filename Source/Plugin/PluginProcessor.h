#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Core/HysteresisProcessor.h"
#include "DSP/Oversampling.h"
#include "DSP/DCBlocker.h"
#include "../../core/include/core/magnetics/DynamicLosses.h"

// =============================================================================
// PluginProcessor — JUCE AudioProcessor for the Phase 1 "Hysteresis Lab" plugin.
//
// Exposes all J-A parameters as automatable plugin parameters.
// Processing chain: Input Gain → 8x Upsample → Hysteresis → DC Block → Downsample → Output Gain
//
// Parameters correspond to real physical quantities — no "Drive" or "Character"
// knobs. The user controls the magnetic material properties directly.
// =============================================================================

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    // ─── AudioProcessor interface ─────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // ─── Editor ───────────────────────────────────────────────────────────────
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ─── Program / state ──────────────────────────────────────────────────────
    const juce::String getName() const override { return "Transformer Model"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ─── Parameter tree ───────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

private:
    // Parameter tree
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // DSP components — one per channel (max 2 for stereo)
    static constexpr int maxChannels = 2;
    HysteresisProcessor hysteresis[maxChannels];
    DCBlocker           dcBlocker[maxChannels];
    transfo::DynamicLosses dynamicLosses[maxChannels];
    OversamplingWrapper oversampling;

    // Cached parameter values
    std::atomic<float>* inputLevelParam  = nullptr;
    std::atomic<float>* outputLevelParam = nullptr;
    std::atomic<float>* msParam          = nullptr;
    std::atomic<float>* aParam           = nullptr;
    std::atomic<float>* kParam           = nullptr;
    std::atomic<float>* cParam           = nullptr;
    std::atomic<float>* alphaParam       = nullptr;
    std::atomic<float>* osOrderParam     = nullptr;
    std::atomic<float>* kEddyParam     = nullptr;
    std::atomic<float>* kExcessParam   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
