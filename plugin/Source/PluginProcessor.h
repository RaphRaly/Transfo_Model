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

#include "../../core/include/core/magnetics/AnhystereticFunctions.h"
#include "../../core/include/core/magnetics/CPWLLeaf.h"
#include "../../core/include/core/magnetics/JilesAthertonLeaf.h"
#include "../../core/include/core/model/Presets.h"
#include "../../core/include/core/model/ToleranceModel.h"
#include "../../core/include/core/model/TransformerModel.h"
#include "../../core/include/core/preamp/PreampModel.h"
#include "../../core/include/core/harrison/HarrisonMicPre.h"
#include "ParameterLayout.h"

class PluginProcessor : public juce::AudioProcessor {
public:
  PluginProcessor();
  ~PluginProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "Transformer Model"; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  // A2.2: Latency compensation — reports correct latency based on processing mode
  void updateLatencyReport();

  // P1.2: Peak saturation percentage (0-100) for GUI meter
  float getPeakSaturation() {
    const int m = static_cast<int>(modeParam_->load());
    if (m == 0)
      return realtimeModel_[0].getPeakSaturation();
    else
      return physicalModel_[0].getPeakSaturation();
  }

  // P1.3: Dynamic Lm readout (Henry) for engineering info panel
  float getLmSmoothed() {
    const int m = static_cast<int>(modeParam_->load());
    if (m == 0)
      return realtimeModel_[0].getLmSmoothed();
    else
      return physicalModel_[0].getLmSmoothed();
  }

  juce::AudioProcessorValueTreeState &getAPVTS() { return apvts_; }

  // Monitoring data for editor
  transfo::TransformerModel<transfo::CPWLLeaf>::MonitorData
  getMonitorData() const {
    return realtimeModel_[0].getMonitorData();
  }

  // Preamp monitoring data for editor (Sprint 7)
  transfo::PreampModel<transfo::JilesAthertonLeaf<transfo::LangevinPade>>::MonitorData
  getPreampMonitorData() const {
    return preampModel_[0].getMonitorData();
  }

  size_t readBHSamples(transfo::BHSample *dest, size_t maxSamples) {
    const int modeIndex = static_cast<int>(modeParam_->load());
    if (modeIndex == 0)
      return realtimeModel_[0].readBHSamples(dest, maxSamples);
    else
      return physicalModel_[0].readBHSamples(dest, maxSamples);
  }

private:
  juce::AudioProcessorValueTreeState apvts_;

  // DSP: one model per channel (stereo)
  static constexpr int kMaxChannels = 2;
  transfo::TransformerModel<transfo::CPWLLeaf> realtimeModel_[kMaxChannels];
  transfo::TransformerModel<transfo::JilesAthertonLeaf<transfo::LangevinPade>>
      physicalModel_[kMaxChannels];

  transfo::ToleranceModel toleranceModel_;

  // Cached parameter pointers
  std::atomic<float> *inputGainParam_ = nullptr;
  std::atomic<float> *outputGainParam_ = nullptr;
  std::atomic<float> *mixParam_ = nullptr;
  std::atomic<float> *presetParam_ = nullptr;
  std::atomic<float> *modeParam_ = nullptr;
  std::atomic<float> *svuParam_ = nullptr;
  std::atomic<float> *circuitParam_ = nullptr;

  int lastPresetIndex_ = -1;
  int lastModeIndex_ = 0;    // A2.2: Track mode for latency updates

  void applyPreset(int presetIndex);

  // Preamp model (one per channel, stereo) — Sprint 7
  transfo::PreampModel<transfo::JilesAthertonLeaf<transfo::LangevinPade>>
      preampModel_[kMaxChannels];

  // Cached preamp parameter pointers
  std::atomic<float> *preampGainParam_ = nullptr;
  std::atomic<float> *preampPathParam_ = nullptr;
  std::atomic<float> *preampPadParam_ = nullptr;
  std::atomic<float> *preampRatioParam_ = nullptr;
  std::atomic<float> *preampPhaseParam_ = nullptr;
  std::atomic<float> *preampEnabledParam_ = nullptr;

  // Harrison Console Mic Pre (one per channel, stereo)
  transfo::TransformerModel<transfo::CPWLLeaf> harrisonTransformer_[kMaxChannels];
  Harrison::MicPre::HarrisonMicPre<transfo::TransformerModel<transfo::CPWLLeaf>>
      harrisonMicPre_[kMaxChannels];

  // Cached Harrison parameter pointers
  std::atomic<float> *harrisonMicGainParam_ = nullptr;
  std::atomic<float> *harrisonPadParam_     = nullptr;
  std::atomic<float> *harrisonPhaseParam_   = nullptr;
  std::atomic<float> *harrisonSourceZParam_ = nullptr;

  // T2 Output Transformer Load (Sprint C.3)
  std::atomic<float> *t2LoadParam_ = nullptr;
  int lastT2LoadIndex_ = 1;  // Track for change detection (default: 10k bridging)

  // Dry buffer for Harrison mix processing
  std::vector<float> harrisonDryBuffer_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
