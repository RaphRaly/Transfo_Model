// =============================================================================
// simulate.cpp — CLI tool for offline transformer simulation.
//
// Usage:
//   simulate [options]
//
// Options:
//   --preset <0>           Transformer preset (0=Jensen JT-115K-E)
//   --freq <Hz>            Input sine frequency (default: 1000)
//   --level <dBFS>         Input level in dBFS (default: -6)
//   --duration <seconds>   Duration in seconds (default: 1.0)
//   --samplerate <Hz>      Sample rate (default: 44100)
//   --output <file>        Output file (raw float32 LE, default: stdout)
//   --csv                  Output as CSV (sample_index, input, output)
//
// Examples:
//   simulate --preset 0 --freq 60 --level -10 --duration 0.5 --output out.raw
//   simulate --preset 1 --freq 1000 --csv > result.csv
//
// Processes in Realtime mode (CPWL + ADAA) by default.
// =============================================================================

#include "../core/include/core/model/TransformerModel.h"
#include "../core/include/core/model/Presets.h"
#include "../core/include/core/magnetics/CPWLLeaf.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static void printUsage()
{
    std::cerr << "Usage: simulate [options]\n"
              << "  --preset <0>           Transformer (0=Jensen JT-115K-E)\n"
              << "  --freq <Hz>           Sine frequency (default: 1000)\n"
              << "  --level <dBFS>        Input level (default: -6)\n"
              << "  --duration <s>        Duration (default: 1.0)\n"
              << "  --samplerate <Hz>     Sample rate (default: 44100)\n"
              << "  --output <file>       Output file (raw float32 LE)\n"
              << "  --csv                 CSV output to stdout\n"
              << std::endl;
}

int main(int argc, char* argv[])
{
    // Defaults
    int    preset     = 0;
    float  freq       = 1000.0f;
    float  levelDB    = -6.0f;
    float  duration   = 1.0f;
    float  sampleRate = 44100.0f;
    std::string outputFile;
    bool   csvMode    = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--preset") == 0 && i + 1 < argc)
            preset = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--freq") == 0 && i + 1 < argc)
            freq = static_cast<float>(std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--level") == 0 && i + 1 < argc)
            levelDB = static_cast<float>(std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
            duration = static_cast<float>(std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--samplerate") == 0 && i + 1 < argc)
            sampleRate = static_cast<float>(std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            outputFile = argv[++i];
        else if (std::strcmp(argv[i], "--csv") == 0)
            csvMode = true;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            printUsage();
            return 0;
        }
        else
        {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage();
            return 1;
        }
    }

    // Clamp preset
    if (preset < 0 || preset > 14) preset = 0;

    const int numSamples = static_cast<int>(duration * sampleRate);
    const float amplitude = std::pow(10.0f, levelDB / 20.0f);
    const float omega = 2.0f * 3.14159265358979f * freq / sampleRate;

    // Setup model
    transfo::TransformerModel<transfo::CPWLLeaf> model;
    model.setConfig(transfo::Presets::getByIndex(preset));
    model.setProcessingMode(transfo::ProcessingMode::Realtime);
    model.prepareToPlay(sampleRate, 512);
    model.setInputGain(0.0f);
    model.setOutputGain(0.0f);
    model.setMix(1.0f);

    std::cerr << "Simulating: " << transfo::Presets::getNameByIndex(preset)
              << " | " << freq << " Hz | " << levelDB << " dBFS | "
              << duration << "s @ " << sampleRate << " Hz"
              << std::endl;

    // Generate and process
    std::vector<float> inputBuf(512);
    std::vector<float> outputBuf(512);
    std::vector<float> allInput;
    std::vector<float> allOutput;
    allInput.reserve(numSamples);
    allOutput.reserve(numSamples);

    int remaining = numSamples;
    int sampleIdx = 0;

    while (remaining > 0)
    {
        int blockSize = std::min(remaining, 512);

        for (int k = 0; k < blockSize; ++k)
        {
            inputBuf[k] = amplitude * std::sin(omega * static_cast<float>(sampleIdx + k));
        }

        model.processBlock(inputBuf.data(), outputBuf.data(), blockSize);

        for (int k = 0; k < blockSize; ++k)
        {
            allInput.push_back(inputBuf[k]);
            allOutput.push_back(outputBuf[k]);
        }

        sampleIdx += blockSize;
        remaining -= blockSize;
    }

    // Output
    if (csvMode)
    {
        std::cout << "sample,input,output\n";
        for (int i = 0; i < numSamples; ++i)
            std::printf("%d,%.8e,%.8e\n", i, allInput[i], allOutput[i]);
    }
    else if (!outputFile.empty())
    {
        std::ofstream ofs(outputFile, std::ios::binary);
        if (!ofs)
        {
            std::cerr << "Error: cannot open " << outputFile << "\n";
            return 1;
        }
        ofs.write(reinterpret_cast<const char*>(allOutput.data()),
                  numSamples * sizeof(float));
        std::cerr << "Wrote " << numSamples << " samples to " << outputFile << "\n";
    }
    else
    {
        // Raw float32 to stdout
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::fwrite(allOutput.data(), sizeof(float), numSamples, stdout);
        std::cerr << "Wrote " << numSamples << " samples to stdout (raw float32 LE)\n";
    }

    // Report stats
    auto mon = model.getMonitorData();
    std::cerr << "Last iter count: " << mon.lastIterCount
              << " | Converged: " << (mon.lastConverged ? "yes" : "no")
              << " | Convergence failures: " << mon.convergenceFailures
              << std::endl;

    return 0;
}
