// =============================================================================
// generate_demo_audio.cpp — Headless demo audio generator (P2.3)
//
// Generates A/B WAV pairs (bypass vs processed) for each preset at multiple
// drive levels, plus frequency sweeps.
//
// Usage: generate_demo_audio [output_dir]
// Default output: data/demo_audio/
// =============================================================================

#include <core/model/TransformerModel.h>
#include <core/model/Presets.h>
#include <core/magnetics/CPWLLeaf.h>
#include <core/magnetics/JilesAthertonLeaf.h>
#include <core/magnetics/AnhystereticFunctions.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cctype>

using namespace transfo;

// ── Minimal 16-bit PCM WAV writer ────────────────────────────────────────────
namespace wav {

static void writeLE16(std::ofstream& f, int16_t v) {
    f.write(reinterpret_cast<const char*>(&v), 2);
}
static void writeLE32(std::ofstream& f, int32_t v) {
    f.write(reinterpret_cast<const char*>(&v), 4);
}

static bool write(const std::string& path, const std::vector<float>& samples,
                  int sampleRate, int numChannels = 1)
{
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    int numSamples = static_cast<int>(samples.size());
    int dataSize = numSamples * 2;
    int fileSize = 36 + dataSize;

    file.write("RIFF", 4);
    writeLE32(file, fileSize);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    writeLE32(file, 16);
    writeLE16(file, 1);  // PCM
    writeLE16(file, static_cast<int16_t>(numChannels));
    writeLE32(file, sampleRate);
    writeLE32(file, sampleRate * numChannels * 2);
    writeLE16(file, static_cast<int16_t>(numChannels * 2));
    writeLE16(file, 16);

    file.write("data", 4);
    writeLE32(file, dataSize);

    for (int i = 0; i < numSamples; ++i) {
        float s = std::clamp(samples[static_cast<size_t>(i)], -1.0f, 1.0f);
        int16_t sample = static_cast<int16_t>(s * 32767.0f);
        writeLE16(file, sample);
    }
    return true;
}
} // namespace wav

// ── Signal generators ────────────────────────────────────────────────────────
static std::vector<float> generateSine(float freq, float amplitude,
                                        float duration, float sampleRate)
{
    int n = static_cast<int>(duration * sampleRate);
    std::vector<float> sig(static_cast<size_t>(n));
    const float w = 2.0f * 3.14159265f * freq / sampleRate;
    for (int i = 0; i < n; ++i)
        sig[static_cast<size_t>(i)] = amplitude * std::sin(w * static_cast<float>(i));
    return sig;
}

static std::vector<float> generateChirp(float f0, float f1, float amplitude,
                                         float duration, float sampleRate)
{
    int n = static_cast<int>(duration * sampleRate);
    std::vector<float> sig(static_cast<size_t>(n));
    float k = (f1 - f0) / duration;
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        sig[static_cast<size_t>(i)] = amplitude * std::sin(
            2.0f * 3.14159265f * (f0 * t + 0.5f * k * t * t));
    }
    return sig;
}

// ── Process through transformer model ────────────────────────────────────────
static std::vector<float> processSignal(const std::vector<float>& input,
                                         int presetIdx, float sampleRate,
                                         float inputGainDb = 0.0f)
{
    TransformerModel<CPWLLeaf> model;
    model.setConfig(Presets::getByIndex(presetIdx));
    model.setProcessingMode(ProcessingMode::Realtime);

    model.setInputGain(inputGainDb);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);
    model.prepareToPlay(sampleRate, 512);

    std::vector<float> output(input.size());
    int remaining = static_cast<int>(input.size());
    int offset = 0;

    while (remaining > 0) {
        int block = std::min(remaining, 512);
        model.processBlock(input.data() + offset, output.data() + offset, block);
        offset += block;
        remaining -= block;
    }
    return output;
}

// ── Sanitize filename ────────────────────────────────────────────────────────
static std::string sanitize(const std::string& name)
{
    std::string safe;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-')
            safe += c;
        else if (c == ' ')
            safe += '_';
    }
    return safe;
}

int main(int argc, char* argv[])
{
    std::string outDir = "data/demo_audio";
    if (argc > 1) outDir = argv[1];

    // Create output directory
#ifdef _WIN32
    std::string cmd = "mkdir \"" + outDir + "\" 2>nul";
#else
    std::string cmd = "mkdir -p \"" + outDir + "\"";
#endif
    std::system(cmd.c_str());

    const float sr = 44100.0f;
    const float dur = 3.0f;

    std::printf("TWISTERION Demo Audio Generator\n");
    std::printf("================================\n");
    std::printf("Output: %s/\n\n", outDir.c_str());

    const float drives[] = {0.0f, 3.0f, 6.0f, 10.0f};
    const char* driveNames[] = {"clean", "plus3", "plus6", "plus10"};

    for (int p = 0; p < Presets::kFactoryCount; ++p)
    {
        std::string name = Presets::getNameByIndex(p);
        std::string safe = sanitize(name);
        std::printf("  [%2d] %s\n", p, name.c_str());

        // DI bass at each drive level
        auto bass = generateSine(41.2f, 0.5f, dur, sr);
        for (int d = 0; d < 4; ++d) {
            auto proc = processSignal(bass, p, sr, drives[d]);
            wav::write(outDir + "/" + safe + "_bass_" + driveNames[d] + "_bypass.wav",
                       bass, static_cast<int>(sr));
            wav::write(outDir + "/" + safe + "_bass_" + driveNames[d] + "_processed.wav",
                       proc, static_cast<int>(sr));
        }

        // Frequency sweep
        auto sweep = generateChirp(20.0f, 20000.0f, 0.3f, dur, sr);
        auto sweepProc = processSignal(sweep, p, sr, 0.0f);
        wav::write(outDir + "/" + safe + "_sweep_bypass.wav", sweep, static_cast<int>(sr));
        wav::write(outDir + "/" + safe + "_sweep_processed.wav", sweepProc, static_cast<int>(sr));
    }

    // ── P2.3: Targeted demo scenarios ──────────────────────────────────────

    // Vocal stem simulation: 300 Hz fundamental + harmonics through Neve preset
    // Heritage (Neve 10468) vs Modern (Jensen JT-115K-E) comparison
    {
        std::printf("\n  -- Vocal Demo (Heritage vs Modern) --\n");
        const float vocalDur = 3.0f;
        const int vocalN = static_cast<int>(vocalDur * sr);
        std::vector<float> vocal(static_cast<size_t>(vocalN));
        for (int i = 0; i < vocalN; ++i) {
            float t = static_cast<float>(i) / sr;
            // Vocal-like signal: fundamental + 2nd/3rd harmonics + vibrato
            float vibrato = 1.0f + 0.005f * std::sin(2.0f * 3.14159265f * 5.5f * t);
            float f0 = 300.0f * vibrato;
            vocal[static_cast<size_t>(i)] = 0.3f * (
                std::sin(2.0f * 3.14159265f * f0 * t)
              + 0.4f * std::sin(2.0f * 3.14159265f * 2.0f * f0 * t)
              + 0.15f * std::sin(2.0f * 3.14159265f * 3.0f * f0 * t));
        }

        // Neve 10468 (Heritage) — preset index 2
        auto vocalNeve = processSignal(vocal, 2, sr, 0.0f);
        wav::write(outDir + "/vocal_heritage_bypass.wav", vocal, static_cast<int>(sr));
        wav::write(outDir + "/vocal_heritage_processed.wav", vocalNeve, static_cast<int>(sr));

        // Jensen JT-115K-E (Modern) — preset index 0
        auto vocalJensen = processSignal(vocal, 0, sr, 0.0f);
        wav::write(outDir + "/vocal_modern_bypass.wav", vocal, static_cast<int>(sr));
        wav::write(outDir + "/vocal_modern_processed.wav", vocalJensen, static_cast<int>(sr));
        std::printf("    vocal_heritage + vocal_modern generated\n");
    }

    // Drum bus simulation through API AP2503 preset
    {
        std::printf("  -- Drum Bus Demo (API AP2503) --\n");
        const float drumDur = 2.0f;
        const int drumN = static_cast<int>(drumDur * sr);
        std::vector<float> drums(static_cast<size_t>(drumN));
        for (int i = 0; i < drumN; ++i) {
            float t = static_cast<float>(i) / sr;
            // Kick + snare simulation: exponential decay bursts
            float kick = 0.0f, snare = 0.0f;
            // Kick hits at 0.0s, 0.5s, 1.0s, 1.5s
            for (float hitT : {0.0f, 0.5f, 1.0f, 1.5f}) {
                float dt = t - hitT;
                if (dt >= 0.0f && dt < 0.2f) {
                    kick += 0.6f * std::exp(-dt * 20.0f)
                          * std::sin(2.0f * 3.14159265f * (60.0f - 30.0f * dt) * dt);
                }
            }
            // Snare hits at 0.25s, 0.75s, 1.25s, 1.75s
            for (float hitT : {0.25f, 0.75f, 1.25f, 1.75f}) {
                float dt = t - hitT;
                if (dt >= 0.0f && dt < 0.1f) {
                    snare += 0.4f * std::exp(-dt * 40.0f)
                           * std::sin(2.0f * 3.14159265f * 200.0f * dt);
                }
            }
            drums[static_cast<size_t>(i)] = kick + snare;
        }

        // API AP2503 — preset index 4
        auto drumsProc = processSignal(drums, 4, sr, 3.0f);
        wav::write(outDir + "/drumbus_api_bypass.wav", drums, static_cast<int>(sr));
        wav::write(outDir + "/drumbus_api_processed.wav", drumsProc, static_cast<int>(sr));
        std::printf("    drumbus_api generated\n");
    }

    std::printf("\nDone! %d presets x %d drives x 2 (A/B) + sweeps + vocal/drum demos\n",
                Presets::kFactoryCount, 4);
    return 0;
}
