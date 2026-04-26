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
#include "../../core/include/core/harrison/HarrisonMicPre.h"
#include "ParameterLayout.h"

#include <atomic>

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

  // A2.2: Latency compensation. Caller passes the active circuit so we can
  // double the OS halfband round-trip for the cascaded Double Legacy path.
  void updateLatencyReport();

  // Snapshot of input/output peak levels driven by the active engine.
  // Updated once per processBlock; the editor smooths via LevelMeterComponent.
  struct EngineLevels {
    float inputLevel_dBu  = -120.0f;
    float outputLevel_dBu = -120.0f;
    bool  isClipping      = false;
  };

  EngineLevels getEngineLevels() const {
    EngineLevels lv;
    lv.inputLevel_dBu  = levelInDbu_.load(std::memory_order_relaxed);
    lv.outputLevel_dBu = levelOutDbu_.load(std::memory_order_relaxed);
    lv.isClipping      = isClipping_.load(std::memory_order_relaxed);
    return lv;
  }

  // P1.2: Peak saturation percentage (0-100) for GUI meter
  float getPeakSaturation() {
    const int circuitIndex = static_cast<int>(circuitParam_->load());
    const int modeIndex = static_cast<int>(modeParam_->load());

    if (circuitIndex == 2)
      return harrisonTransformer_[0].getPeakSaturation();

    if (circuitIndex == 0) {
      if (modeIndex == 0)
        return doubleLegacyInputRealtime_[0].getPeakSaturation();
      return doubleLegacyInputPhysical_[0].getPeakSaturation();
    }

    if (modeIndex == 0)
      return realtimeModel_[0].getPeakSaturation();
    return physicalModel_[0].getPeakSaturation();
  }

  // P1.3: Dynamic Lm readout (Henry) for engineering info panel
  float getLmSmoothed() {
    const int circuitIndex = static_cast<int>(circuitParam_->load());
    const int modeIndex = static_cast<int>(modeParam_->load());

    if (circuitIndex == 2)
      return harrisonTransformer_[0].getLmSmoothed();

    if (circuitIndex == 0) {
      if (modeIndex == 0)
        return doubleLegacyInputRealtime_[0].getLmSmoothed();
      return doubleLegacyInputPhysical_[0].getLmSmoothed();
    }

    if (modeIndex == 0)
      return realtimeModel_[0].getLmSmoothed();
    return physicalModel_[0].getLmSmoothed();
  }

  juce::AudioProcessorValueTreeState &getAPVTS() { return apvts_; }

  // Monitoring data for editor
  transfo::TransformerModel<transfo::CPWLLeaf>::MonitorData
  getMonitorData() const {
    const int circuitIndex = static_cast<int>(circuitParam_->load());
    if (circuitIndex == 0)
      return doubleLegacyInputRealtime_[0].getMonitorData();
    return realtimeModel_[0].getMonitorData();
  }

  size_t readBHSamples(transfo::BHSample *dest, size_t maxSamples) {
    const int circuitIndex = static_cast<int>(circuitParam_->load());
    const int modeIndex = static_cast<int>(modeParam_->load());

    if (circuitIndex == 2)
      return harrisonTransformer_[0].readBHSamples(dest, maxSamples);

    if (circuitIndex == 0) {
      if (modeIndex == 0)
        return doubleLegacyInputRealtime_[0].readBHSamples(dest, maxSamples);
      return doubleLegacyInputPhysical_[0].readBHSamples(dest, maxSamples);
    }

    if (modeIndex == 0)
      return realtimeModel_[0].readBHSamples(dest, maxSamples);
    return physicalModel_[0].readBHSamples(dest, maxSamples);
  }

private:
  using RealtimeTransformerT = transfo::TransformerModel<transfo::CPWLLeaf>;
  using PhysicalTransformerT =
      transfo::TransformerModel<
          transfo::JilesAthertonLeaf<transfo::LangevinPade>>;

  juce::AudioProcessorValueTreeState apvts_;

  // DSP: one model per channel (stereo)
  static constexpr int kMaxChannels = 2;
  RealtimeTransformerT realtimeModel_[kMaxChannels];
  PhysicalTransformerT physicalModel_[kMaxChannels];
  RealtimeTransformerT doubleLegacyInputRealtime_[kMaxChannels];
  RealtimeTransformerT doubleLegacyOutputRealtime_[kMaxChannels];
  PhysicalTransformerT doubleLegacyInputPhysical_[kMaxChannels];
  PhysicalTransformerT doubleLegacyOutputPhysical_[kMaxChannels];

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
  int lastModeCircuitIndex_ = -1;

  void applyPreset(int presetIndex);
  void applyDoubleLegacyConfigs(int t2LoadIndex);
  void prepareLegacyPhysicalModels(float sampleRate, int samplesPerBlock, int osFactor);
  void prepareDoubleLegacyPhysicalModels(float sampleRate, int samplesPerBlock, int osFactor);

  // Engine-level peak snapshots, refreshed once per processBlock.
  std::atomic<float> levelInDbu_{-120.0f};
  std::atomic<float> levelOutDbu_{-120.0f};
  std::atomic<bool>  isClipping_{false};

  // Harrison Console Mic Pre (one per channel, stereo)
  // Uses full J-A + Bertotti engine (not CPWL) so the JT-115K-E material
  // hysteresis and dynamic losses are audible in the mic-pre path.
  using HarrisonTransformerT = PhysicalTransformerT;
  HarrisonTransformerT harrisonTransformer_[kMaxChannels];
  Harrison::MicPre::HarrisonMicPre<HarrisonTransformerT>
      harrisonMicPre_[kMaxChannels];

  // Cached Harrison parameter pointers
  std::atomic<float> *harrisonMicGainParam_ = nullptr;
  std::atomic<float> *harrisonPadParam_     = nullptr;
  std::atomic<float> *harrisonPhaseParam_   = nullptr;
  std::atomic<float> *harrisonSourceZParam_ = nullptr;
  std::atomic<float> *harrisonDynLossParam_ = nullptr;

  // T2 Output Transformer Load (Sprint C.3)
  std::atomic<float> *t2LoadParam_ = nullptr;
  int lastT2LoadIndex_ = 1;  // Track for change detection (default: 10k bridging)

  // Dry buffer for Harrison mix processing
  std::vector<float> harrisonDryBuffer_;
  std::vector<float> doubleLegacyDryBuffer_;
  std::vector<float> doubleLegacyMidBuffer_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
